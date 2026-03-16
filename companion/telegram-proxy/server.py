"""
Telegram MTProto proxy server for ESP32 companion.

Exposes a JSON REST API and a WebSocket push endpoint so that
resource-constrained devices can interact with Telegram without
implementing MTProto themselves.
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import logging
import os
import sys
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional

import yaml
from aiohttp import web
from telethon import TelegramClient, events, functions
from telethon.errors import (
    AuthRestartError,
    PhoneCodeInvalidError,
    SessionPasswordNeededError,
    PasswordHashInvalidError,
    SendCodeUnavailableError,
)
from telethon.sessions import StringSession
from telethon.tl.types import (
    User,
    Chat,
    Channel,
    Dialog,
    Message,
    ForumTopic,
)
from telethon.tl.functions.messages import GetForumTopicsRequest

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("telegram-proxy")

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

CONFIG_PATH = Path(__file__).parent / "config.yaml"
BRIDGE_RULES_PATH = Path(__file__).parent / "bridge_rules.yaml"


def load_config() -> dict[str, Any]:
    with CONFIG_PATH.open() as fh:
        return yaml.safe_load(fh)


# ---------------------------------------------------------------------------
# Bridge rules
# ---------------------------------------------------------------------------

VALID_DIRECTIONS = {"mesh_to_telegram", "telegram_to_mesh", "both"}


@dataclass
class BridgeRule:
    name: str
    direction: str
    mesh_channel: str
    telegram_chat_id: int
    telegram_topic_id: Optional[int] = None
    format_template: str = "{node_name}: {text}"
    enabled: bool = True

    def to_dict(self) -> dict[str, Any]:
        d = asdict(self)
        return d

    @staticmethod
    def from_dict(d: dict[str, Any]) -> "BridgeRule":
        return BridgeRule(
            name=str(d["name"]),
            direction=str(d.get("direction", "mesh_to_telegram")),
            mesh_channel=str(d.get("mesh_channel", "")),
            telegram_chat_id=int(d["telegram_chat_id"]),
            telegram_topic_id=int(d["telegram_topic_id"]) if d.get("telegram_topic_id") is not None else None,
            format_template=str(d.get("format_template", "{node_name}: {text}")),
            enabled=bool(d.get("enabled", True)),
        )


def _load_bridge_rules() -> dict[str, BridgeRule]:
    if not BRIDGE_RULES_PATH.exists():
        return {}
    try:
        with BRIDGE_RULES_PATH.open() as fh:
            raw = yaml.safe_load(fh) or []
        return {r["name"]: BridgeRule.from_dict(r) for r in raw if isinstance(r, dict) and r.get("name")}
    except Exception:
        log.exception("Failed to load bridge rules from %s", BRIDGE_RULES_PATH)
        return {}


def _save_bridge_rules(rules: dict[str, BridgeRule]) -> None:
    try:
        data = [r.to_dict() for r in rules.values()]
        with BRIDGE_RULES_PATH.open("w") as fh:
            yaml.safe_dump(data, fh, allow_unicode=True, default_flow_style=False)
    except Exception:
        log.exception("Failed to save bridge rules to %s", BRIDGE_RULES_PATH)


# ---------------------------------------------------------------------------
# Application state
# ---------------------------------------------------------------------------

class AppState:
    def __init__(self) -> None:
        self.client: Optional[TelegramClient] = None
        self.phone_code_hash: Optional[str] = None
        self.phone: Optional[str] = None
        self.ws_clients: set[web.WebSocketResponse] = set()
        self.qr_login: Optional[Any] = None
        self.qr_wait_task: Optional[asyncio.Task[Any]] = None
        self.qr_status: str = "idle"
        self.qr_url: Optional[str] = None
        self.qr_expires_at: Optional[int] = None
        self.qr_error: Optional[str] = None
        self.qr_generation: int = 0
        self.bridge_rules: dict[str, BridgeRule] = _load_bridge_rules()


# ---------------------------------------------------------------------------
# Helper utilities
# ---------------------------------------------------------------------------

def _dialog_type(entity: Any) -> str:
    if isinstance(entity, User):
        return "user"
    if isinstance(entity, Channel):
        return "channel"
    return "group"


def _message_to_dict(msg: Message, client: TelegramClient) -> dict[str, Any]:
    from_name = ""
    if msg.sender:
        sender = msg.sender
        if hasattr(sender, "first_name") and sender.first_name:
            from_name = sender.first_name
            if hasattr(sender, "last_name") and sender.last_name:
                from_name += f" {sender.last_name}"
        elif hasattr(sender, "title") and sender.title:
            from_name = sender.title
    return {
        "id": msg.id,
        "from_name": from_name,
        "text": msg.text or "",
        "date": int(msg.date.timestamp()) if msg.date else 0,
        "out": bool(msg.out),
    }


async def _push_to_ws_clients(state: AppState, payload: dict[str, Any]) -> None:
    if not state.ws_clients:
        return
    data = json.dumps(payload)
    dead: set[web.WebSocketResponse] = set()
    for ws in state.ws_clients:
        try:
            await ws.send_str(data)
        except Exception:
            dead.add(ws)
    state.ws_clients -= dead


def _normalize_phone(raw_phone: Any) -> str:
    phone = str(raw_phone or "").strip()
    if not phone:
        return ""

    prefix = "+" if phone.startswith("+") else ""
    digits = "".join(ch for ch in phone if ch.isdigit())
    return f"{prefix}{digits}" if digits else phone


def _sent_code_type_name(sent_code_type: Any) -> str:
    if sent_code_type is None:
        return "unknown"

    type_name = type(sent_code_type).__name__.lower()
    if "missedcall" in type_name:
        return "missed_call"
    if "flashcall" in type_name:
        return "flash_call"
    if "fragmentsms" in type_name:
        return "fragment_sms"
    if "sms" in type_name:
        return "sms"
    if "app" in type_name:
        return "app"
    if "call" in type_name:
        return "call"
    return type(sent_code_type).__name__


def _sent_code_delivery_hint(sent_code_type: Any) -> str:
    code_type = _sent_code_type_name(sent_code_type)
    pattern = getattr(sent_code_type, "pattern", None)

    if code_type == "app":
        return "Open Telegram on another device and enter the login code shown there."
    if code_type == "sms":
        return "Check SMS messages for the Telegram login code."
    if code_type == "call":
        return "Telegram will deliver the login code with a phone call."
    if code_type == "flash_call":
        if pattern:
            return f"Telegram will place a flash call. Use the caller number matching {pattern}."
        return "Telegram will place a flash call. Use the caller number as instructed by Telegram."
    if code_type == "missed_call":
        if pattern:
            return f"Telegram will place a missed call. The login code is in the caller number digits matching {pattern}."
        return "Telegram will place a missed call. The login code is in the caller number digits."
    if code_type == "fragment_sms":
        return "Check SMS messages for the fragmented Telegram login code."
    return "Enter the login code delivered by Telegram."


def _sent_code_to_dict(sent_code_type: Any) -> dict[str, Any]:
    payload = {
        "code_type": _sent_code_type_name(sent_code_type),
        "delivery_hint": _sent_code_delivery_hint(sent_code_type),
    }

    length = getattr(sent_code_type, "length", None)
    if isinstance(length, int):
        payload["code_length"] = length

    pattern = getattr(sent_code_type, "pattern", None)
    if pattern:
        payload["code_pattern"] = pattern

    prefix = getattr(sent_code_type, "prefix", None)
    if prefix:
        payload["code_prefix"] = prefix

    return payload


def _safe_int(value: Any, default: int, min_val: int = 0, max_val: int = 100) -> int:
    try:
        return max(min_val, min(max_val, int(value)))
    except (ValueError, TypeError):
        return default


def _json_bool(value: Any, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return value.strip().lower() not in {"", "0", "false", "no", "off"}
    return bool(value)


async def _ensure_client_connected(state: AppState) -> TelegramClient:
    client = state.client
    if client is None:
        raise web.HTTPInternalServerError(reason="Telegram client not initialised")

    if client.is_connected():
        return client

    try:
        await client.connect()
    except Exception as exc:
        log.exception("Telegram client reconnect failed")
        raise web.HTTPServiceUnavailable(reason=f"Telegram client is unavailable: {exc}")

    return client


async def _maybe_resend_code(
    client: TelegramClient,
    phone: str,
    phone_code_hash: str,
    sent_code: Any,
) -> tuple[Any, bool]:
    initial_type = _sent_code_type_name(getattr(sent_code, "type", None))
    if initial_type != "app":
        return sent_code, False

    try:
        resent = await client(functions.auth.ResendCodeRequest(
            phone_number=phone,
            phone_code_hash=phone_code_hash,
        ))
        resent_type = _sent_code_type_name(getattr(resent, "type", None))
        log.info("Requested fallback delivery for login code: %s -> %s", initial_type, resent_type)
        return resent, True
    except SendCodeUnavailableError:
        log.info("Fallback delivery is unavailable for %s", phone)
        return sent_code, False
    except AuthRestartError:
        log.warning("Telegram requested auth restart while resending code for %s", phone)
        return sent_code, False
    except Exception:
        log.exception("Fallback resend failed for %s", phone)
        return sent_code, False


async def _update_state_phone(state: AppState, client: TelegramClient) -> None:
    try:
        user: User = await client.get_me()
    except Exception:
        log.exception("Unable to fetch current Telegram user")
        return

    if user and user.phone:
        state.phone = user.phone


async def _cancel_qr_wait_task(state: AppState) -> None:
    task = state.qr_wait_task
    state.qr_wait_task = None
    if task is None or task.done():
        return

    task.cancel()
    with contextlib.suppress(asyncio.CancelledError):
        await task


def _clear_qr_state(state: AppState) -> None:
    state.qr_login = None
    state.qr_status = "idle"
    state.qr_url = None
    state.qr_expires_at = None
    state.qr_error = None


def _qr_status_payload(state: AppState) -> dict[str, Any]:
    status = state.qr_status
    if status == "pending" and state.qr_expires_at and state.qr_expires_at <= int(time.time()):
        status = "expired"

    payload: dict[str, Any] = {"status": status}

    if status == "pending":
        payload["qr_url"] = state.qr_url
        payload["expires_at"] = state.qr_expires_at
        payload["instruction"] = "Scan this QR in Telegram: Settings > Devices > Link Desktop Device."
    elif status == "2fa_required":
        payload["instruction"] = "QR confirmed. Enter your Telegram 2FA password."
    elif status == "expired":
        payload["instruction"] = "QR code expired. Refresh to get a new one."
    elif status == "error":
        payload["error"] = state.qr_error or "QR login failed"

    return payload


async def _wait_for_qr_login(state: AppState, generation: int, qr_login: Any) -> None:
    try:
        user: User = await qr_login.wait()
        if generation != state.qr_generation:
            return
        _clear_qr_state(state)
        state.qr_status = "authorized"
        if user and user.phone:
            state.phone = user.phone
        log.info("QR login completed successfully")
    except SessionPasswordNeededError:
        if generation != state.qr_generation:
            return
        _clear_qr_state(state)
        state.qr_status = "2fa_required"
        log.info("QR login completed, waiting for 2FA password")
    except asyncio.TimeoutError:
        if generation != state.qr_generation:
            return
        _clear_qr_state(state)
        state.qr_status = "expired"
        log.info("QR login expired before scan completed")
    except asyncio.CancelledError:
        raise
    except Exception as exc:
        if generation != state.qr_generation:
            return
        _clear_qr_state(state)
        state.qr_status = "error"
        state.qr_error = str(exc)
        log.exception("QR login wait failed")
    finally:
        if generation == state.qr_generation:
            state.qr_wait_task = None


async def _start_qr_login(state: AppState, force_refresh: bool = False) -> dict[str, Any]:
    client = await _ensure_client_connected(state)
    state.phone_code_hash = None
    if await client.is_user_authorized():
        await _update_state_phone(state, client)
        await _cancel_qr_wait_task(state)
        _clear_qr_state(state)
        state.qr_status = "authorized"
        return _qr_status_payload(state)

    now = int(time.time())
    if (
        not force_refresh
        and state.qr_status == "pending"
        and state.qr_url
        and state.qr_expires_at
        and state.qr_expires_at > now
    ):
        return _qr_status_payload(state)

    await _cancel_qr_wait_task(state)
    state.qr_generation += 1
    generation = state.qr_generation

    qr_login = await client.qr_login()
    state.qr_login = qr_login
    state.qr_status = "pending"
    state.qr_url = qr_login.url
    state.qr_expires_at = int(qr_login.expires.timestamp())
    state.qr_error = None
    state.qr_wait_task = asyncio.create_task(_wait_for_qr_login(state, generation, qr_login))
    log.info("Created QR login token (expires at %s)", state.qr_expires_at)
    return _qr_status_payload(state)


# ---------------------------------------------------------------------------
# Auth handlers
# ---------------------------------------------------------------------------

async def auth_status(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    try:
        client = await _ensure_client_connected(state)
        authorized = await client.is_user_authorized()
        if authorized and not state.phone:
            await _update_state_phone(state, client)
    except Exception:
        authorized = False
    return web.json_response({"authorized": authorized, "phone": state.phone if authorized else None})


async def auth_phone(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    try:
        body = await request.json()
    except Exception:
        raise web.HTTPBadRequest(reason="Invalid JSON body")

    phone = _normalize_phone(body.get("phone"))
    if not phone:
        raise web.HTTPBadRequest(reason="Missing 'phone' field")

    client = await _ensure_client_connected(state)
    fallback_if_app = _json_bool(body.get("fallback_if_app"), default=True)

    try:
        state.phone = None
        state.phone_code_hash = None
        await _cancel_qr_wait_task(state)
        _clear_qr_state(state)

        result = await client.send_code_request(phone)
        initial_code_type = _sent_code_type_name(result.type)

        if fallback_if_app:
            result, resent = await _maybe_resend_code(client, phone, result.phone_code_hash, result)
        else:
            resent = False

        state.phone = phone
        state.phone_code_hash = result.phone_code_hash

        response = {
            "status": "code_required",
            **_sent_code_to_dict(result.type),
        }
        if resent:
            response["resent"] = True
            response["initial_code_type"] = initial_code_type

        next_type = getattr(result, "next_type", None)
        if next_type is not None:
            response["next_code_type"] = _sent_code_type_name(next_type)

        timeout = getattr(result, "timeout", None)
        if isinstance(timeout, int):
            response["timeout"] = timeout

        log.info(
            "Code sent via: %s (next: %s, timeout: %s, resent: %s)",
            response["code_type"],
            response.get("next_code_type", "none"),
            response.get("timeout", "n/a"),
            response.get("resent", False),
        )

        return web.json_response(response)
    except Exception as exc:
        state.phone = None
        state.phone_code_hash = None
        log.exception("send_code_request failed")
        raise web.HTTPInternalServerError(reason=str(exc))


async def auth_code(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    try:
        body = await request.json()
    except Exception:
        raise web.HTTPBadRequest(reason="Invalid JSON body")

    code = str(body.get("code") or "").strip()
    if not code:
        raise web.HTTPBadRequest(reason="Missing 'code' field")
    if not state.phone or not state.phone_code_hash:
        raise web.HTTPBadRequest(reason="No pending auth — call /api/auth/phone first")

    client = await _ensure_client_connected(state)

    try:
        await client.sign_in(
            phone=state.phone,
            code=code,
            phone_code_hash=state.phone_code_hash,
        )
        state.phone_code_hash = None
        await _cancel_qr_wait_task(state)
        _clear_qr_state(state)
        state.qr_status = "authorized"
        await _update_state_phone(state, client)
        return web.json_response({"status": "authorized"})
    except SessionPasswordNeededError:
        return web.json_response({"status": "2fa_required"})
    except PhoneCodeInvalidError:
        raise web.HTTPUnauthorized(reason="Invalid verification code")
    except Exception as exc:
        log.exception("sign_in failed")
        raise web.HTTPInternalServerError(reason=str(exc))


async def auth_password(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    try:
        body = await request.json()
    except Exception:
        raise web.HTTPBadRequest(reason="Invalid JSON body")

    password = body.get("password")
    if not password:
        raise web.HTTPBadRequest(reason="Missing 'password' field")

    client = await _ensure_client_connected(state)

    try:
        await client.sign_in(password=password)
        await _cancel_qr_wait_task(state)
        _clear_qr_state(state)
        state.qr_status = "authorized"
        await _update_state_phone(state, client)
        return web.json_response({"status": "authorized"})
    except PasswordHashInvalidError:
        raise web.HTTPUnauthorized(reason="Incorrect 2FA password")
    except Exception as exc:
        log.exception("2FA sign_in failed")
        raise web.HTTPInternalServerError(reason=str(exc))


async def auth_logout(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    client = state.client
    await _cancel_qr_wait_task(state)
    _clear_qr_state(state)
    if client and client.is_connected():
        try:
            await client.log_out()
        except Exception:
            log.exception("log_out failed")
    state.phone = None
    state.phone_code_hash = None
    return web.json_response({"status": "logged_out"})


async def auth_qr_start(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    try:
        body = await request.json()
    except Exception:
        body = {}

    force_refresh = _json_bool(body.get("force"), default=False)

    try:
        payload = await _start_qr_login(state, force_refresh=force_refresh)
        return web.json_response(payload)
    except Exception as exc:
        log.exception("qr_login start failed")
        raise web.HTTPInternalServerError(reason=str(exc))


async def auth_qr_status(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    client = state.client

    if client and client.is_connected():
        try:
            if await client.is_user_authorized():
                await _update_state_phone(state, client)
                await _cancel_qr_wait_task(state)
                _clear_qr_state(state)
                state.qr_status = "authorized"
        except Exception:
            log.exception("qr_login status check failed")

    return web.json_response(_qr_status_payload(state))


# ---------------------------------------------------------------------------
# Data handlers
# ---------------------------------------------------------------------------

async def _require_auth(state: AppState) -> TelegramClient:
    """Return the connected, authorised client or raise HTTP 401."""
    client = await _ensure_client_connected(state)
    if not await client.is_user_authorized():
        raise web.HTTPUnauthorized(reason="Not authorised — complete auth flow first")
    return client


async def me(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    client = await _require_auth(state)
    try:
        user: User = await client.get_me()
        return web.json_response({
            "id": user.id,
            "first_name": user.first_name or "",
            "phone": user.phone or "",
        })
    except Exception as exc:
        log.exception("get_me failed")
        raise web.HTTPInternalServerError(reason=str(exc))


async def chats(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    client = await _require_auth(state)

    limit = _safe_int(request.rel_url.query.get("limit", 20), default=20, min_val=1, max_val=100)
    offset_id = _safe_int(request.rel_url.query.get("offset_id", 0), default=0, min_val=0, max_val=2147483647)
    log.info("GET /api/chats limit=%d offset_id=%d", limit, offset_id)

    try:
        dialogs: list[Dialog] = await client.get_dialogs(limit=limit, offset_id=offset_id)
    except Exception as exc:
        log.exception("get_dialogs failed")
        raise web.HTTPInternalServerError(reason=str(exc))

    result = []
    for dialog in dialogs:
        entity = dialog.entity
        last_msg = dialog.message
        last_text = ""
        last_date = 0
        if last_msg and hasattr(last_msg, "text"):
            last_text = last_msg.text or ""
            if last_msg.date:
                last_date = int(last_msg.date.timestamp())

        title = ""
        if hasattr(entity, "title") and entity.title:
            title = entity.title
        elif hasattr(entity, "first_name"):
            title = entity.first_name or ""
            if hasattr(entity, "last_name") and entity.last_name:
                title += f" {entity.last_name}"

        is_forum = getattr(entity, "forum", False) or False
        result.append({
            "id": dialog.id,
            "title": title,
            "unread": dialog.unread_count,
            "type": _dialog_type(entity),
            "last_message": last_text,
            "last_date": last_date,
            "is_forum": is_forum,
        })

    return web.json_response(result, dumps=lambda obj: json.dumps(obj, ensure_ascii=False))


async def messages(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    client = await _require_auth(state)

    # Support both /api/messages/{chat_id} (path) and /api/messages?chat_id=N (query)
    raw_chat_id = request.match_info.get("chat_id") or request.rel_url.query.get("chat_id")
    log.info("GET /api/messages raw_chat_id=%r query=%s", raw_chat_id, dict(request.rel_url.query))
    if not raw_chat_id:
        raise web.HTTPBadRequest(reason="Missing 'chat_id'")
    try:
        chat_id = int(raw_chat_id)
    except ValueError:
        log.warning("Invalid chat_id received: %r (full URL: %s)", raw_chat_id, request.rel_url)
        raise web.HTTPBadRequest(reason=f"Invalid 'chat_id': {raw_chat_id!r}")
    limit = _safe_int(request.rel_url.query.get("limit", 20), default=20, min_val=1, max_val=100)
    offset_id = _safe_int(request.rel_url.query.get("offset_id", 0), default=0, min_val=0, max_val=2147483647)
    raw_topic_id = request.rel_url.query.get("topic_id")
    topic_id = int(raw_topic_id) if raw_topic_id else None
    log.info("Fetching messages for chat_id=%d topic_id=%s limit=%d offset_id=%d", chat_id, topic_id, limit, offset_id)

    try:
        kwargs = {"limit": limit, "offset_id": offset_id}
        if topic_id is not None:
            kwargs["reply_to"] = topic_id
        msgs = await client.get_messages(chat_id, **kwargs)
    except Exception as exc:
        log.exception("get_messages failed for chat_id=%d topic_id=%s", chat_id, topic_id)
        raise web.HTTPInternalServerError(reason=str(exc))

    result = [_message_to_dict(msg, client) for msg in msgs if isinstance(msg, Message)]
    log.info("Returning %d messages for chat_id=%d", len(result), chat_id)
    return web.json_response(result, dumps=lambda obj: json.dumps(obj, ensure_ascii=False))


async def topics(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    client = await _require_auth(state)

    raw_chat_id = request.rel_url.query.get("chat_id")
    if not raw_chat_id:
        raise web.HTTPBadRequest(reason="Missing 'chat_id'")
    try:
        chat_id = int(raw_chat_id)
    except ValueError:
        raise web.HTTPBadRequest(reason=f"Invalid 'chat_id': {raw_chat_id!r}")

    log.info("GET /api/topics chat_id=%d", chat_id)

    try:
        entity = await client.get_entity(chat_id)
        result_topics = await client(GetForumTopicsRequest(
            channel=entity,
            offset_date=0,
            offset_id=0,
            offset_topic=0,
            limit=100,
        ))
    except Exception as exc:
        log.exception("get_forum_topics failed for chat_id=%d", chat_id)
        raise web.HTTPInternalServerError(reason=str(exc))

    result = []
    for t in result_topics.topics:
        if not isinstance(t, ForumTopic):
            continue
        result.append({
            "id": t.id,
            "title": t.title,
            "unread": getattr(t, "unread_count", 0) or 0,
        })

    log.info("Returning %d topics for chat_id=%d", len(result), chat_id)
    return web.json_response(result, dumps=lambda obj: json.dumps(obj, ensure_ascii=False))


async def send_message(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    client = await _require_auth(state)

    try:
        body = await request.json()
    except Exception:
        raise web.HTTPBadRequest(reason="Invalid JSON body")

    chat_id = body.get("chat_id")
    text = body.get("text")
    topic_id = body.get("topic_id")
    log.info("POST /api/send chat_id=%r topic_id=%r text=%r", chat_id, topic_id, text[:80] if text else text)
    if chat_id is None or not text:
        raise web.HTTPBadRequest(reason="Missing 'chat_id' or 'text' field")

    try:
        kwargs: dict[str, Any] = {}
        if topic_id is not None:
            kwargs["reply_to"] = int(topic_id)
        sent: Message = await client.send_message(chat_id, text, **kwargs)
        log.info("Message sent: id=%d to chat_id=%s topic_id=%s", sent.id, chat_id, topic_id)
        return web.json_response({"status": "sent", "message_id": sent.id})
    except Exception as exc:
        log.exception("send_message failed for chat_id=%s", chat_id)
        raise web.HTTPInternalServerError(reason=str(exc))


# ---------------------------------------------------------------------------
# Bridge handlers
# ---------------------------------------------------------------------------

async def bridge_list_rules(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    return web.json_response([r.to_dict() for r in state.bridge_rules.values()])


async def bridge_create_rule(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    try:
        body = await request.json()
    except Exception:
        raise web.HTTPBadRequest(reason="Invalid JSON body")

    name = str(body.get("name") or "").strip()
    if not name:
        raise web.HTTPBadRequest(reason="Missing 'name' field")

    direction = str(body.get("direction") or "mesh_to_telegram")
    if direction not in VALID_DIRECTIONS:
        raise web.HTTPBadRequest(reason=f"'direction' must be one of: {', '.join(sorted(VALID_DIRECTIONS))}")

    mesh_channel = str(body.get("mesh_channel") or "").strip()
    if not mesh_channel:
        raise web.HTTPBadRequest(reason="Missing 'mesh_channel' field")

    raw_chat_id = body.get("telegram_chat_id")
    if raw_chat_id is None:
        raise web.HTTPBadRequest(reason="Missing 'telegram_chat_id' field")
    try:
        telegram_chat_id = int(raw_chat_id)
    except (ValueError, TypeError):
        raise web.HTTPBadRequest(reason="'telegram_chat_id' must be an integer")

    raw_topic_id = body.get("telegram_topic_id")
    telegram_topic_id: Optional[int] = None
    if raw_topic_id is not None:
        try:
            telegram_topic_id = int(raw_topic_id)
        except (ValueError, TypeError):
            raise web.HTTPBadRequest(reason="'telegram_topic_id' must be an integer")

    format_template = str(body.get("format_template") or "{node_name}: {text}")
    enabled = _json_bool(body.get("enabled"), default=True)

    rule = BridgeRule(
        name=name,
        direction=direction,
        mesh_channel=mesh_channel,
        telegram_chat_id=telegram_chat_id,
        telegram_topic_id=telegram_topic_id,
        format_template=format_template,
        enabled=enabled,
    )
    state.bridge_rules[name] = rule
    _save_bridge_rules(state.bridge_rules)
    log.info("Bridge rule %r saved (direction=%s chat_id=%d)", name, direction, telegram_chat_id)
    return web.json_response(rule.to_dict(), status=200)


async def bridge_delete_rule(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    name = request.match_info["name"]
    if name not in state.bridge_rules:
        raise web.HTTPNotFound(reason=f"Bridge rule {name!r} not found")
    del state.bridge_rules[name]
    _save_bridge_rules(state.bridge_rules)
    log.info("Bridge rule %r deleted", name)
    return web.json_response({"status": "deleted", "name": name})


async def bridge_forward(request: web.Request) -> web.Response:
    """Called by ESP32 to forward a mesh message to Telegram."""
    state: AppState = request.app["state"]
    try:
        body = await request.json()
    except Exception:
        raise web.HTTPBadRequest(reason="Invalid JSON body")

    node_name = str(body.get("node_name") or "").strip()
    channel = str(body.get("channel") or "").strip()
    text = str(body.get("text") or "").strip()

    if not channel or not text:
        raise web.HTTPBadRequest(reason="Missing 'channel' or 'text' field")

    if not state.client:
        raise web.HTTPServiceUnavailable(reason="Telegram client not initialised")

    matched = [
        r for r in state.bridge_rules.values()
        if r.enabled
        and r.direction in ("mesh_to_telegram", "both")
        and r.mesh_channel == channel
    ]

    if not matched:
        return web.json_response({"status": "no_match", "forwarded": 0})

    forwarded = 0
    errors = []
    for rule in matched:
        formatted = rule.format_template.format(
            node_name=node_name,
            text=text,
            channel=channel,
            sender=node_name,
        )
        try:
            kwargs: dict[str, Any] = {}
            if rule.telegram_topic_id is not None:
                kwargs["reply_to"] = rule.telegram_topic_id
            await state.client.send_message(rule.telegram_chat_id, formatted, **kwargs)
            forwarded += 1
            log.info(
                "Bridge forwarded mesh->telegram: rule=%r chat_id=%d node=%r channel=%r",
                rule.name, rule.telegram_chat_id, node_name, channel,
            )
        except Exception as exc:
            log.exception("Bridge forward failed for rule %r", rule.name)
            errors.append({"rule": rule.name, "error": str(exc)})

    response: dict[str, Any] = {"status": "ok", "forwarded": forwarded}
    if errors:
        response["errors"] = errors
    return web.json_response(response)


# ---------------------------------------------------------------------------
# WebSocket handler
# ---------------------------------------------------------------------------

async def ws_updates(request: web.Request) -> web.WebSocketResponse:
    state: AppState = request.app["state"]
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    state.ws_clients.add(ws)
    log.info("WebSocket client connected (%d total)", len(state.ws_clients))
    try:
        async for _ in ws:
            pass  # Ignore incoming frames; connection is push-only
    finally:
        state.ws_clients.discard(ws)
        log.info("WebSocket client disconnected (%d total)", len(state.ws_clients))
    return ws


# ---------------------------------------------------------------------------
# Application lifecycle
# ---------------------------------------------------------------------------

async def on_startup(app: web.Application) -> None:
    config = app["config"]
    state: AppState = app["state"]
    tg_cfg = config["telegram"]

    api_id: int = tg_cfg["api_id"]
    api_hash: str = tg_cfg["api_hash"]
    session_name: str = tg_cfg.get("session_name", "meshtastic_companion")

    # Use a file session so credentials survive restarts.
    session_dir = Path(__file__).parent / "sessions"
    session_dir.mkdir(parents=True, exist_ok=True)
    session_path = session_dir / session_name
    client = TelegramClient(str(session_path), api_id, api_hash)

    @client.on(events.NewMessage)
    async def _on_new_message(event: events.NewMessage.Event) -> None:
        msg: Message = event.message
        sender = await event.get_sender()
        from_name = ""
        if sender:
            if hasattr(sender, "first_name") and sender.first_name:
                from_name = sender.first_name
                if hasattr(sender, "last_name") and sender.last_name:
                    from_name += f" {sender.last_name}"
            elif hasattr(sender, "title") and sender.title:
                from_name = sender.title

        payload = {
            "type": "new_message",
            "chat_id": event.chat_id,
            "message": {
                "id": msg.id,
                "from_name": from_name,
                "text": msg.text or "",
                "date": int(msg.date.timestamp()) if msg.date else 0,
                "out": bool(msg.out),
            },
        }
        await _push_to_ws_clients(state, payload)

        # Telegram→Mesh bridge: push matching messages to ESP32 via WebSocket
        if msg.text and not msg.out:
            tg_rules = [
                r for r in state.bridge_rules.values()
                if r.enabled
                and r.direction in ("telegram_to_mesh", "both")
                and r.telegram_chat_id == event.chat_id
            ]
            for rule in tg_rules:
                formatted = rule.format_template.format(
                    node_name=from_name,
                    text=msg.text,
                    channel=rule.mesh_channel,
                    sender=from_name,
                )
                bridge_payload = {
                    "type": "bridge_message",
                    "rule": rule.name,
                    "mesh_channel": rule.mesh_channel,
                    "from_name": from_name,
                    "text": formatted,
                    "original_text": msg.text,
                }
                await _push_to_ws_clients(state, bridge_payload)
                log.info(
                    "Bridge pushed telegram->mesh: rule=%r channel=%r from=%r",
                    rule.name, rule.mesh_channel, from_name,
                )

    await client.connect()
    state.client = client
    log.info("Telegram client connected (authorised: %s)", await client.is_user_authorized())


async def on_shutdown(app: web.Application) -> None:
    state: AppState = app["state"]
    # Close all WebSocket connections gracefully.
    for ws in list(state.ws_clients):
        await ws.close()
    state.ws_clients.clear()
    await _cancel_qr_wait_task(state)
    _clear_qr_state(state)
    # Disconnect Telegram client.
    if state.client and state.client.is_connected():
        await state.client.disconnect()
    log.info("Server shut down cleanly")


# ---------------------------------------------------------------------------
# API key authentication middleware
# ---------------------------------------------------------------------------

_PROXY_API_KEY: Optional[str] = os.environ.get("PROXY_API_KEY") or None


@web.middleware
async def api_key_middleware(request: web.Request, handler: Any) -> web.StreamResponse:
    """Enforce X-API-Key header on all /api/ endpoints when PROXY_API_KEY is set."""
    if _PROXY_API_KEY and request.path.startswith("/api/"):
        provided = request.headers.get("X-API-Key", "")
        if provided != _PROXY_API_KEY:
            log.warning("Rejected request to %s: invalid or missing X-API-Key", request.path)
            raise web.HTTPForbidden(reason="Invalid or missing X-API-Key")
    return await handler(request)


# ---------------------------------------------------------------------------
# Application factory
# ---------------------------------------------------------------------------

def build_app(config: dict[str, Any]) -> web.Application:
    state = AppState()
    app = web.Application(middlewares=[api_key_middleware])
    app["config"] = config
    app["state"] = state

    app.on_startup.append(on_startup)
    app.on_shutdown.append(on_shutdown)

    router = app.router
    # Auth
    router.add_get("/api/status", auth_status)  # alias used by ESP32 firmware
    router.add_get("/api/auth/status", auth_status)
    router.add_post("/api/auth/phone", auth_phone)
    router.add_post("/api/auth/code", auth_code)
    router.add_post("/api/auth/password", auth_password)
    router.add_post("/api/auth/2fa", auth_password)  # alias used by ESP32 firmware
    router.add_post("/api/auth/qr/start", auth_qr_start)
    router.add_get("/api/auth/qr/status", auth_qr_status)
    router.add_post("/api/auth/logout", auth_logout)
    # Data
    router.add_get("/api/me", me)
    router.add_get("/api/chats", chats)
    router.add_get("/api/topics", topics)
    router.add_get("/api/messages/{chat_id}", messages)
    router.add_get("/api/messages", messages)  # query-param style: ?chat_id=N
    router.add_post("/api/send", send_message)
    # Bridge
    router.add_get("/api/bridge/rules", bridge_list_rules)
    router.add_post("/api/bridge/rules", bridge_create_rule)
    router.add_delete("/api/bridge/rules/{name}", bridge_delete_rule)
    router.add_post("/api/bridge/forward", bridge_forward)
    # WebSocket
    router.add_get("/api/updates", ws_updates)

    return app


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    config = load_config()
    server_cfg = config.get("server", {})
    host = server_cfg.get("host", "0.0.0.0")
    port = int(server_cfg.get("port", 8080))

    app = build_app(config)
    log.info("Starting Telegram proxy on %s:%d", host, port)
    web.run_app(app, host=host, port=port)


if __name__ == "__main__":
    main()

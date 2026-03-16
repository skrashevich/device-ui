# Telegram Proxy Companion Server

A Python server that acts as a proxy between an ESP32 device and Telegram,
using Telethon for MTProto user authentication.

## Setup

1. Get `api_id` and `api_hash` from https://my.telegram.org
2. Edit `config.yaml` with your credentials
3. Install dependencies:
   ```
   pip install -r requirements.txt
   ```
4. Run the server:
   ```
   python server.py
   ```

## API

The server exposes a JSON REST API on `http://host:port/api/` and a WebSocket
endpoint at `ws://host:port/api/updates` for real-time message push.

### Auth flow

1. `GET /api/auth/status` — check if already authorized
2. `POST /api/auth/phone` with `{"phone": "+1234567890"}` — request SMS code
3. `POST /api/auth/code` with `{"code": "12345"}` — confirm code
4. If 2FA is enabled: `POST /api/auth/password` with `{"password": "..."}`
5. `POST /api/auth/logout` — sign out

### Data endpoints

- `GET /api/me` — current user info
- `GET /api/chats?limit=20&offset_id=0` — list dialogs
- `GET /api/messages/{chat_id}?limit=20&offset_id=0` — message history
- `POST /api/send` with `{"chat_id": int, "text": str}` — send message

### WebSocket push

Connect to `ws://host:port/api/updates` to receive real-time events:

```json
{
  "type": "new_message",
  "chat_id": 123456789,
  "message": {
    "id": 42,
    "from_name": "John Doe",
    "text": "Hello!",
    "date": 1710000000,
    "out": false
  }
}
```

"""
Mock server for a biometric attendance device activation system.

Run with:
    python mock_server.py

Then it's available at http://localhost:8000
"""

from datetime import datetime

import uvicorn
from fastapi import FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware

app = FastAPI(title="Device Activation Mock Server")

# --- CORS: allow any origin to call this API -------------------------------
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

# --- Config ------------------------------------------------------------------
# device_id -> the ONE registration_code that's valid for it.
# Any other code, even if it's a well-formed 12-char uppercase/alphanumeric
# string, is rejected. Add more entries here to mock additional devices.
VALID_ACTIVATIONS = {
    "P001-2607-6AEC-YRH5": "QWERTYUIOPAS",
}

ALLOWED_PARAMS = {"device_id"}


# --- Log every incoming request to the console -------------------------------
@app.middleware("http")
async def log_requests(request: Request, call_next):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(
        f"[{timestamp}] {request.method} {request.url.path} "
        f"| query={dict(request.query_params)} "
        f"| client={request.client.host if request.client else 'unknown'}"
    )
    response = await call_next(request)
    print(f"[{timestamp}] -> responded {response.status_code}")
    return response


# --- Endpoint ------------------------------------------------------------------
@app.post("/api/devices/registerDevice")
async def register_device(request: Request, device_id: str):
    # Only device_id may be present in the query string
    unexpected = set(request.query_params.keys()) - ALLOWED_PARAMS
    if unexpected:
        print(f"[REJECTED] Unexpected parameter(s): {sorted(unexpected)}")
        raise HTTPException(
            status_code=400,
            detail=f"Unexpected parameter(s): {', '.join(sorted(unexpected))}",
        )

    # 1. Extract the activation code (token) from the Authorization header
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        print("[REJECTED] Missing or invalid Authorization header")
        raise HTTPException(status_code=401, detail="Unauthorized: Missing token")
    
    registration_code = auth_header.split(" ")[1]

    # Exact match only: this device_id must map to exactly this
    # registration_code. Any other code is rejected.
    is_valid = VALID_ACTIVATIONS.get(device_id) == registration_code

    if is_valid:
        return {
            "success": True,
            "activated": True,
            "message": "Device activated successfully",
        }

    return {
        "success": False,
        "activated": False,
        "message": "Invalid device_id or registration_code",
    }


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
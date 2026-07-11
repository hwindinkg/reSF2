#!/usr/bin/env python3
"""Generate an ed25519 SSH keypair for reSF2 git push.

Outputs:
  /home/z/.ssh/id_ed25519_resf2       (private key, chmod 600)
  /home/z/.ssh/id_ed25519_resf2.pub   (public key, to add to GitHub)

Prints the public key to stdout in OpenSSH format.
"""
import os
import sys
import base64
import struct

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

KEY_PATH = "/home/z/.ssh/id_ed25519_resf2"
PUB_PATH = KEY_PATH + ".pub"
COMMENT = "resf2-agent"


def ssh_ed25519_pub_b64(priv: Ed25519PrivateKey) -> str:
    """Encode the public key in OpenSSH wire format and base64 it."""
    pub_bytes = priv.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )
    # OpenSSH public-key wire format:
    #   string  "ssh-ed25519"
    #   string  <32-byte public key>
    blob = b"ssh-ed25519" + b"\x00\x00\x00\x0b" + b"ssh-ed25519" \
        + b"\x00\x00\x00\x20" + pub_bytes
    # Length-prefixed: first 4 bytes = length of "ssh-ed25519" (11)
    blob = struct.pack(">I", 11) + b"ssh-ed25519" \
        + struct.pack(">I", 32) + pub_bytes
    return base64.b64encode(blob).decode("ascii")


def main() -> int:
    os.makedirs(os.path.dirname(KEY_PATH), exist_ok=True)
    os.chmod(os.path.dirname(KEY_PATH), 0o700)

    if os.path.exists(KEY_PATH):
        # Idempotent: don't clobber an existing key.
        print(f"[skip] {KEY_PATH} already exists", file=sys.stderr)
    else:
        priv = Ed25519PrivateKey.generate()
        priv_pem = priv.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.OpenSSH,
            encryption_algorithm=serialization.NoEncryption(),
        )
        with open(KEY_PATH, "wb") as f:
            f.write(priv_pem)
        os.chmod(KEY_PATH, 0o600)
        print(f"[ok] wrote private key: {KEY_PATH}", file=sys.stderr)

        pub_b64 = ssh_ed25519_pub_b64(priv)
        pub_line = f"ssh-ed25519 {pub_b64} {COMMENT}\n"
        with open(PUB_PATH, "w") as f:
            f.write(pub_line)
        os.chmod(PUB_PATH, 0o644)
        print(f"[ok] wrote public key:  {PUB_PATH}", file=sys.stderr)

    # Always print the public key for the user to copy.
    with open(PUB_PATH, "r") as f:
        sys.stdout.write(f.read())
    return 0


if __name__ == "__main__":
    sys.exit(main())

import os
import shutil
import sqlite3
import json
import base64
from pathlib import Path
from datetime import datetime
import win32crypt
from Crypto.Cipher import AES

# simple test to let you see what passwords a stealer could steal
# assume anything can be stolen even if [Decryption Failed]
# it may fail from my simple decryption, surely there should be better algorithms than this file

HOME = Path.home()

REPORT = {
    "timestamp": str(datetime.utcnow()),
    "browsers": []
}

BROWSERS = {
    "Chrome": HOME / r"AppData/Local/Google/Chrome/User Data",
    "Edge": HOME / r"AppData/Local/Microsoft/Edge/User Data",
    "Brave": HOME / r"AppData/Local/BraveSoftware/Brave-Browser/User Data",
}

TARGET_FILES = [
    "Login Data",
    "Cookies",
    "Web Data",
    "History"
]


def safe_copy(src):
    temp = Path("temp_db.sqlite")
    try:
        shutil.copy2(src, temp)
        return temp
    except:
        return None


def get_chrome_master_key(local_state_path):
    try:
        with open(local_state_path, 'r', encoding='utf-8') as f:
            local_state = json.loads(f.read())

        # The key is base64 encoded and prefixed with 'DPAPI'
        encrypted_key = base64.b64decode(local_state["os_crypt"]["encrypted_key"])
        encrypted_key = encrypted_key[5:]  

        master_key = win32crypt.CryptUnprotectData(encrypted_key, None, None, None, 0)[1]
        return master_key
    except Exception as e:
        print(f"Failed to get master key from {local_state_path}: {e}")
        return None


def decrypt_password(encrypted_blob, master_key):
    try:
        # Chrome format: version (3 bytes) | nonce (12 bytes) | ciphertext | tag (16 bytes)
        nonce = encrypted_blob[3:15]
        ciphertext = encrypted_blob[15:-16]
        tag = encrypted_blob[-16:]

        cipher = AES.new(master_key, AES.MODE_GCM, nonce=nonce)
        decrypted_pass = cipher.decrypt_and_verify(ciphertext, tag)
        return decrypted_pass.decode()
    except Exception:
        # Fallback for older Chrome 
        try:
            return win32crypt.CryptUnprotectData(encrypted_blob, None, None, None, 0)[1].decode()
        except Exception:
            return "[Decryption Failed]"


def analyze_login_db(db_path, browser_user_data_path):
    """
    Extracts login credentials and decrypts the passwords.
    browser_user_data_path is the root User Data folder (e.g., .../Chrome/User Data)
    """
    findings = []

    temp_db = safe_copy(db_path)
    if not temp_db:
        return findings

    # Locate the Local State file
    local_state_path = browser_user_data_path / "Local State"
    if not local_state_path.exists():
        findings.append({"error": "Local State file not found"})
        return findings

    master_key = get_chrome_master_key(local_state_path)
    if not master_key:
        findings.append({"error": "Could not retrieve master key"})
        return findings

    try:
        conn = sqlite3.connect(temp_db)
        cur = conn.cursor()

        # Select the password_value column
        cur.execute("""
            SELECT origin_url, username_value, password_value
            FROM logins
        """)

        rows = cur.fetchall()

        for row in rows:
            url = row[0]
            username = row[1]
            encrypted_password = row[2]

            if encrypted_password:
                password = decrypt_password(encrypted_password, master_key)
            else:
                password = ""

            findings.append({
                "url": url,
                "username": username,
                "password": password
            })

        conn.close()

    except Exception as e:
        findings.append({"error": str(e)})

    try:
        os.remove(temp_db)
    except:
        pass

    return findings


def analyze_cookie_db(db_path):
    findings = []

    temp_db = safe_copy(db_path)
    if not temp_db:
        return findings

    try:
        conn = sqlite3.connect(temp_db)
        cur = conn.cursor()

        cur.execute("""
            SELECT host_key, name
            FROM cookies
            LIMIT 100
        """)

        rows = cur.fetchall()

        for row in rows:
            findings.append({
                "domain": row[0],
                "cookie_name": row[1]
            })

        conn.close()

    except Exception as e:
        findings.append({"error": str(e)})

    try:
        os.remove(temp_db)
    except:
        pass

    return findings


# Main traversal
for browser_name, browser_path in BROWSERS.items():

    if not browser_path.exists():
        continue

    browser_result = {
        "browser": browser_name,
        "profiles": []
    }

    for item in browser_path.iterdir():

        if not item.is_dir():
            continue

        if "Default" not in item.name and "Profile" not in item.name:
            continue

        profile = {
            "profile": item.name,
            "files": [],
            "credential_entries": [],
            "cookie_entries": []
        }

        for root, dirs, files in os.walk(item):

            for file in files:

                if file not in TARGET_FILES:
                    continue

                full_path = Path(root) / file

                stat = full_path.stat()

                file_info = {
                    "file": file,
                    "path": str(full_path),
                    "size_kb": round(stat.st_size / 1024, 2),
                    "last_modified": str(
                        datetime.fromtimestamp(stat.st_mtime)
                    )
                }

                profile["files"].append(file_info)

                if file == "Login Data":
                    # Pass the browser's User Data path
                    profile["credential_entries"] = analyze_login_db(full_path, browser_path)

                if file == "Cookies":
                    profile["cookie_entries"] = analyze_cookie_db(full_path)

        browser_result["profiles"].append(profile)

    REPORT["browsers"].append(browser_result)

with open("browser_exposure_report.json", "w", encoding="utf-8") as f:
    json.dump(REPORT, f, indent=4)

print("[+] Analysis complete.")
print("[+] Output saved to browser_exposure_report.json")
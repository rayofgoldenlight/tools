#!/usr/bin/env python3
import sys
import os
import re
import mimetypes
from pathlib import Path
from urllib.parse import urljoin, urlparse, unquote
from urllib.robotparser import RobotFileParser

import requests
from bs4 import BeautifulSoup

MAX_IMAGES = 10
MAX_BYTES = 5 * 1024 * 1024  # 5 MB
USER_AGENT = None

def get_robot_parser(base_url, session):
    try:
        parsed = urlparse(base_url)
        robots_url = f"{parsed.scheme}://{parsed.netloc}/robots.txt"
        resp = session.get(robots_url, timeout=10)
        rp = RobotFileParser()
        if resp.status_code == 200:
            rp.parse(resp.text.splitlines())
        else:
            rp = None
        return rp
    except requests.RequestException:
        return None

def can_fetch(rp, url):
    if rp is None:
        return True
    try:
        return rp.can_fetch(USER_AGENT, url)
    except Exception:
        return True

def parse_image_urls(html, base_url):
    soup = BeautifulSoup(html, "html.parser")
    urls = []

    # <img src> and srcset
    for img in soup.find_all("img"):
        src = img.get("src")
        if src and not src.strip().startswith("data:"):
            urls.append(urljoin(base_url, src))
        srcset = img.get("srcset")
        if srcset:
            for candidate in srcset.split(","):
                url_part = candidate.strip().split(" ")[0]
                if url_part and not url_part.startswith("data:"):
                    urls.append(urljoin(base_url, url_part))

    # <link rel="image_src">, icons, etc.
    for link in soup.find_all("link"):
        rel = link.get("rel") or []
        if any("image" in r for r in rel):
            href = link.get("href")
            if href:
                urls.append(urljoin(base_url, href))

    # OpenGraph/Twitter meta
    for meta in soup.find_all("meta"):
        prop = (meta.get("property") or meta.get("name") or "").lower()
        if prop in ("og:image", "twitter:image", "twitter:image:src"):
            content = meta.get("content")
            if content:
                urls.append(urljoin(base_url, content))

    # Deduplicate while preserving order
    seen = set()
    deduped = []
    for u in urls:
        if u not in seen:
            seen.add(u)
            deduped.append(u)
    return deduped

def is_image_content_type(headers):
    ct = (headers.get("Content-Type") or "").split(";")[0].strip().lower()
    return ct.startswith("image/"), ct

def guess_extension_from_content_type(ct):
    if not ct:
        return ""
    ext = mimetypes.guess_extension(ct) or ""
    # Normalize some common cases
    if ext == ".jpe":
        ext = ".jpg"
    return ext

def safe_filename_from_url(url):
    name = os.path.basename(urlparse(url).path) or "image"
    name = unquote(name)
    # Strip query-like suffixes from basename (keep extension if present)
    if "?" in name:
        name = name.split("?", 1)[0]
    # Keep alnum, dash, underscore, dot
    name = re.sub(r"[^A-Za-z0-9._-]", "_", name)
    # Avoid leading dot
    name = name.lstrip(".") or "image"
    return name

def ensure_unique_path(base_path: Path):
    if not base_path.exists():
        return base_path
    stem = base_path.stem
    suffix = base_path.suffix
    parent = base_path.parent
    i = 1
    while True:
        candidate = parent / f"{stem}_{i}{suffix}"
        if not candidate.exists():
            return candidate
        i += 1

def download_image_with_cap(url, out_dir: Path, session, max_bytes=MAX_BYTES):
    # Quick robots-safe check is done upstream. Get headers + stream.
    try:
        with session.get(url, stream=True, timeout=20) as r:
            if r.status_code != 200:
                return False, f"HTTP {r.status_code}", None
            # Skip non-images
            is_img, ct = is_image_content_type(r.headers)
            if not is_img:
                return False, "Not an image", None
            # Header-based size check
            cl = r.headers.get("Content-Length")
            if cl:
                try:
                    if int(cl) > max_bytes:
                        return False, f"Too large (Content-Length {cl} bytes)", None
                except ValueError:
                    pass

            # Decide filename
            name = safe_filename_from_url(url)
            # If no extension, add one from content-type
            if "." not in os.path.basename(name) or name.endswith("."):
                ext = guess_extension_from_content_type(ct)
                if ext and not name.endswith(ext):
                    name = f"{name.rstrip('.')}{ext}"

            dest = out_dir / name
            dest = ensure_unique_path(dest)

            downloaded = 0
            tmp_path = dest.with_suffix(dest.suffix + ".part")
            with open(tmp_path, "wb") as f:
                for chunk in r.iter_content(chunk_size=64 * 1024):
                    if not chunk:
                        continue
                    downloaded += len(chunk)
                    if downloaded > max_bytes:
                        f.close()
                        try:
                            tmp_path.unlink(missing_ok=True)
                        except Exception:
                            pass
                        return False, "Exceeded 5MB while downloading", None
                    f.write(chunk)

            tmp_path.rename(dest)
            return True, "OK", dest
    except requests.RequestException as e:
        return False, f"Request error: {e}", None
    except Exception as e:
        return False, f"Error: {e}", None

def main():
    if len(sys.argv) < 2:
        print("Usage: python scrape_images.py <url> [output_dir]")
        sys.exit(1)

    url = sys.argv[1]
    out_dir = Path(sys.argv[2]) if len(sys.argv) >= 3 else Path("downloaded_images")
    out_dir.mkdir(parents=True, exist_ok=True)

    session = requests.Session()
    session.headers.update({
        "User-Agent": USER_AGENT,
        "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    })

    # robots.txt
    rp = get_robot_parser(url, session)
    if not can_fetch(rp, url):
        print("Blocked by robots.txt for the page URL. Exiting.")
        sys.exit(2)

    try:
        resp = session.get(url, timeout=20)
        resp.raise_for_status()
    except requests.RequestException as e:
        print(f"Failed to fetch page: {e}")
        sys.exit(3)

    image_urls = parse_image_urls(resp.text, url)
    if not image_urls:
        print("No images found.")
        return

    print(f"Found {len(image_urls)} image candidates. Attempting to download up to {MAX_IMAGES} images (<= 5MB each).")

    downloaded = 0
    for img_url in image_urls:
        if downloaded >= MAX_IMAGES:
            break
        if not can_fetch(rp, img_url):
            print(f"- Skipping (robots): {img_url}")
            continue
        ok, msg, path = download_image_with_cap(img_url, out_dir, session, MAX_BYTES)
        if ok:
            downloaded += 1
            print(f"+ Downloaded [{downloaded}/{MAX_IMAGES}]: {path.name}")
        else:
            print(f"- Skipped: {img_url} ({msg})")

    print(f"Done. Downloaded {downloaded} images to: {out_dir.resolve()}")

if __name__ == "__main__":
    # Respect site terms of service and copyright. Use responsibly.
    main()
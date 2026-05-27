import os
import requests
from bs4 import BeautifulSoup
from urllib.parse import urljoin
from tqdm import tqdm

# Scraper to grab pdfs off of "index of" sites

# Edit url and folder names

# URL of the "Index of" page
BASE_URL = "http://www.agritech.tnau.ac.in/horticulture/pdf/tech_bulletin/national/"

# Folder for the PDFs
DOWNLOAD_FOLDER = "downloaded_pdfs"

os.makedirs(DOWNLOAD_FOLDER, exist_ok=True)

response = requests.get(BASE_URL)
response.raise_for_status()

soup = BeautifulSoup(response.text, "html.parser")

pdf_links = []

for link in soup.find_all("a"):
    href = link.get("href")

    if href and href.lower().endswith(".pdf"):
        full_url = urljoin(BASE_URL, href)
        pdf_links.append(full_url)

print(f"Found {len(pdf_links)} PDF files")

for pdf_url in pdf_links:
    filename = os.path.basename(pdf_url)
    filepath = os.path.join(DOWNLOAD_FOLDER, filename)

    if os.path.exists(filepath):
        print(f"Skipping (already exists): {filename}")
        continue

    print(f"Downloading: {filename}")
    try:
        # Stream download
        with requests.get(pdf_url, stream=True) as r:
            r.raise_for_status()
            total_size = int(r.headers.get("content-length", 0))  # may be 0 if unknown

            # Progress bar
            with tqdm(total=total_size, unit='B', unit_scale=True, desc=filename) as pbar:
                with open(filepath, "wb") as f:
                    for chunk in r.iter_content(chunk_size=8192):
                        if chunk:
                            f.write(chunk)
                            pbar.update(len(chunk))
    except Exception as e:
        print(f"Failed to download {filename}: {e}")

print("Done!")
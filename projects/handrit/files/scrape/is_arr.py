import os
import requests
from bs4 import BeautifulSoup
from urllib.parse import urljoin, urlparse


# crawl through folders to get pdfs on index of sites

BASE_URL = "https://example.com/files/"
DOWNLOAD_FOLDER = "downloaded_pdfs"

visited = set()

os.makedirs(DOWNLOAD_FOLDER, exist_ok=True)


def crawl(url):
    if url in visited:
        return

    visited.add(url)

    print(f"Crawling: {url}")

    try:
        response = requests.get(url)
        response.raise_for_status()
    except Exception as e:
        print(f"Failed: {url} -> {e}")
        return

    soup = BeautifulSoup(response.text, "html.parser")

    for link in soup.find_all("a"):
        href = link.get("href")

        if not href:
            continue

        full_url = urljoin(url, href)

        # Download PDFs
        if href.lower().endswith(".pdf"):
            filename = os.path.basename(urlparse(full_url).path)
            filepath = os.path.join(DOWNLOAD_FOLDER, filename)

            if not os.path.exists(filepath):
                print(f"Downloading: {filename}")

                pdf_response = requests.get(full_url)

                with open(filepath, "wb") as f:
                    f.write(pdf_response.content)

        # Crawl subdirectories
        elif href.endswith("/") and not href.startswith("../"):
            crawl(full_url)


crawl(BASE_URL)

print("Finished!")
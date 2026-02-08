import requests
from urllib.parse import urlparse

def generate_course_urls(start, end):
    base_url = "https://sites.google.com/view/"
    return [f"{base_url}{x}/modules" for x in range(start, end + 1)]

def check_redirects(urls):
    results = {}
    for url in urls:
        try:
            response = requests.get(url, allow_redirects=True, timeout=10)
            results[url] = response.url
        except requests.RequestException as e:
            results[url] = f"Error: {e}"
    return results

def normalize_url(url):
    parsed = urlparse(url)
    host = parsed.hostname.lower() if parsed.hostname else ""
    path = parsed.path.rstrip("/")  # strip trailing slash
    return f"{parsed.scheme}://{host}{path}"

def find_non_redirecting(start, end):
    urls = generate_course_urls(start, end)
    step2_results = check_redirects(urls)
    non_redirecting = []
    for original, final in step2_results.items():
        if isinstance(final, str) and final.startswith("Error"):
            continue  # skip errors
        if normalize_url(original) == normalize_url(final):
            non_redirecting.append(original)
    return non_redirecting

print(find_non_redirecting(7580, 7590))
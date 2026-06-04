import re
import os
from llama_cpp import Llama

# Complex terms finder, chunked and using qwen2.5-3b for efficiency

# config -- ]]]]] Change INPUT_FILE to your input file, note output file with overwrite what you have
MODEL_PATH = "qwen2.5-3b-instruct-q4_k_m.gguf"
INPUT_FILE = "sd.txt"
OUTPUT_FILE = "detected_terms.txt"
CANDIDATE_BATCH_SIZE = 60    # Terms per LLM classification call
N_GPU_LAYERS = -1
CONTEXT_SIZE = 4096
TEMPERATURE = 0.0


# regex

TECH_EXTENSIONS = re.compile(
    r'\b\w+\.(py|js|ts|jsx|tsx|sh|yaml|yml|json|toml|env|md|sql|go|rs|rb|java|cpp|c|h|cs|php|swift|kt)\b',
    re.IGNORECASE
)
# camelCase
CAMEL_PASCAL = re.compile(r'\b[a-z]+(?:[A-Z][a-z0-9]+)+\b|\b[A-Z][a-z]+(?:[A-Z][a-z0-9]+)+\b')
# ALL_CAPS
ALL_CAPS_ID = re.compile(r'\b[A-Z][A-Z0-9_]{2,}\b')
# Hyphen
HYPHEN_TECH = re.compile(r'\b[a-z]{2,}-[a-z]{2,}(?:-[a-z]{2,})?\b')
# Version strings
VERSION = re.compile(r'\bv?\d+\.\d+(?:\.\d+)?\b')
# @
AT_PACKAGE = re.compile(r'@[a-zA-Z][\w/-]+')
# Flag -- -
CLI_FLAGS = re.compile(r'--?[a-zA-Z][a-zA-Z0-9_-]+')
# Single capitalised words
CAPITALISED = re.compile(r'\b[A-Z][a-z]{2,}\b')

STOPWORDS = {
    # English noise
    "The", "This", "That", "They", "There", "Then", "From", "With",
    "When", "What", "Where", "Who", "How", "Also", "Just", "Have",
    "Been", "Will", "Would", "Could", "Should", "Does", "Make",
    "Your", "Our", "Any", "All", "But", "And", "For", "Not", "Are",
    "Can", "You", "We", "It", "In", "On", "At", "To", "Of", "Is",
    "If", "Or", "As", "By", "An", "Be", "Do", "So", "Up", "Go",
    # Common chat
    "Hey", "Ok", "Yes", "No", "Hi", "Thanks", "Sorry", "Sure",
    "Good", "Great", "Nice", "See", "Get", "Got", "New", "Old",
    "Now", "Out", "Use", "Used", "Using", "Run", "Set", "Add",
    "Put", "Let", "One", "Two", "Far", "Via", "Per", "Way",
    "Work", "Need", "Want", "Find", "Keep", "Best", "Next",
    "Back", "Same", "Each", "More", "Most", "Some", "Many",
    "Very", "Too", "Has", "Had", "Her", "Him", "His", "Its",
    "My", "Me", "Am", "He", "She", "Re", "Vs", "Ex",
    "Fix", "Try", "App", "Log", "Why", "Pro", "Due", "Few",
    "Into", "Once", "Only", "Over", "Even", "Been", "Then",
    "Else", "Both", "Every", "Never", "Still", "Again",
    # Days, months
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
    "Saturday", "Sunday", "January", "February", "March",
    "April", "May", "June", "July", "August", "September",
    "October", "November", "December",
}

def extract_candidates(text: str) -> set:
    """
    Pass 1: pull every token that LOOKS like it could be a tech term.
    No LLM involved — pure pattern matching.
    """
    candidates = set()

    for m in TECH_EXTENSIONS.finditer(text):
        candidates.add(m.group())
    for m in CAMEL_PASCAL.finditer(text):
        candidates.add(m.group())
    for m in ALL_CAPS_ID.finditer(text):
        w = m.group()
        if len(w) <= 2 or w in {"OK", "ID", "API", "URL", "HTTP", "CLI", "UI", "AI", "OS"}:
            if w in {"API", "URL", "HTTP", "CLI", "UI", "AI", "OS", "SDK", "GPU", "CPU",
                     "SSD", "RAM", "SSH", "SSL", "TLS", "DNS", "TCP", "IP", "TS", "JS",
                     "CI", "CD", "PR", "MR", "DB", "ORM", "AWS", "GCP", "GIT"}:
                candidates.add(w)
        else:
            candidates.add(w)
    for m in HYPHEN_TECH.finditer(text):
        candidates.add(m.group())
    for m in AT_PACKAGE.finditer(text):
        candidates.add(m.group())
    for m in CLI_FLAGS.finditer(text):
        flag = m.group().lstrip('-')
        if len(flag) > 3:   # skip single-letter flags
            candidates.add(m.group())
    for m in CAPITALISED.finditer(text):
        w = m.group()
        if w not in STOPWORDS:
            candidates.add(w)

    cleaned = set()
    for c in candidates:
        c = c.strip('.,!?;:\'"()[]{}')
        if not c or len(c) < 2:
            continue
        if re.fullmatch(r'[\d._\-]+', c):   
            continue
        cleaned.add(c)

    return cleaned


# llm -- pass 1

SYSTEM_PROMPT = """\
You are a classifier. You will receive a list of tokens extracted from a chat log.
Your task: decide which ones are real technology terms — libraries, frameworks, tools, \
languages, platforms, protocols, services, file types, or dev concepts.

Rules:
- Only classify tokens from the list provided. Do NOT add new terms.
- Return ONLY the confirmed tech terms as a comma-separated list on a single line.
- If none qualify, return exactly: NONE
- No explanation. No preamble. No markdown. Just the comma-separated list."""

def classify_batch(llm: Llama, candidates: list[str]) -> list[str]:
    """
    Pass 2
    """
    candidate_str = ", ".join(candidates)
    prompt = (
        f"<|im_start|>system\n{SYSTEM_PROMPT}<|im_end|>\n"
        f"<|im_start|>user\n"
        f"Classify these tokens — return only the ones that are real tech terms:\n\n"
        f"{candidate_str}"
        f"<|im_end|>\n"
        f"<|im_start|>assistant\n"
    )

    response = llm(
        prompt,
        max_tokens=256,
        temperature=TEMPERATURE,
        stop=["<|im_end|>"],
        echo=False,
    )

    raw = response['choices'][0]['text'].strip()
    if not raw or raw.upper() == "NONE":
        return []

    results = []
    for term in re.split(r'[,;]', raw):
        term = term.strip().strip('"\'')
        candidates_lower = {c.lower(): c for c in candidates}
        if term.lower() in candidates_lower:
            results.append(candidates_lower[term.lower()])
    return results


def batch(iterable, n):
    lst = list(iterable)
    for i in range(0, len(lst), n):
        yield lst[i:i + n]


def main():
    print("Loading model...")
    llm = Llama(
        model_path=MODEL_PATH,
        n_ctx=CONTEXT_SIZE,
        n_gpu_layers=N_GPU_LAYERS,
        verbose=False,
    )
    print("Model loaded.\n")

    with open(INPUT_FILE, 'r', encoding='utf-8', errors='replace') as f:
        full_text = f.read()

    # Pass 1
    print("Pass 1: extracting candidates with regex...")
    candidates = extract_candidates(full_text)
    print(f"  Found {len(candidates)} unique candidates.\n")

    if not candidates:
        print("No candidates found. Check your input file.")
        return

    # Pass 2
    batches = list(batch(sorted(candidates, key=str.lower), CANDIDATE_BATCH_SIZE))
    print(f"Pass 2: classifying {len(candidates)} candidates in {len(batches)} LLM batches...")

    confirmed = set()
    for i, b in enumerate(batches, 1):
        print(f"  Batch {i}/{len(batches)} ({len(b)} terms)...", end=" ", flush=True)
        results = classify_batch(llm, b)
        confirmed.update(results)
        print(f"confirmed {len(results)}")

    # Output
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        for term in sorted(confirmed, key=str.lower):
            f.write(term + '\n')

    print(f"\nDone. {len(confirmed)} confirmed tech terms → {OUTPUT_FILE}")


if __name__ == "__main__":
    main()
import re
import sys

# slack plaintext chat cleaner

def clean_chat(input_path, output_path):
    ts_re = re.compile(r'^\[?\s*\d{1,2}:\d{2}(?:\s*[APap][Mm])?\s*\]?')
    ts_only_re = re.compile(ts_re.pattern + r'\s*$')
    ts_prefix_re = re.compile(ts_re.pattern + r'\s*(.*)$')

    with open(input_path, 'r', encoding='utf-8') as f:
        raw_lines = [line.strip() for line in f if line.strip() != '']

    n = len(raw_lines)
    i = 0
    speaker = None
    message_parts = []
    output_blocks = [] 

    while i < n:
        line = raw_lines[i]
        parts = re.split(r'\s{2,}', line, maxsplit=1)
        if len(parts) == 2:
            name = parts[0].strip()
            rest = parts[1].strip()
            if ts_only_re.match(rest):
                if message_parts:
                    output_blocks.append((speaker, ' '.join(message_parts)))
                speaker = name
                message_parts = []
                i += 1
                continue

        m = ts_prefix_re.match(line)
        if m:
            rest_text = m.group(1).strip()
            if rest_text:  
                message_parts.append(rest_text)
            i += 1
            continue

  
        if ts_only_re.match(line):
            i += 1
            continue

        if i + 1 < n and ts_only_re.match(raw_lines[i + 1]):

            if message_parts:
                output_blocks.append((speaker, ' '.join(message_parts)))
            speaker = line
            i += 1        

            while i < n and ts_only_re.match(raw_lines[i]):
                i += 1
            message_parts = []
            continue
        else:
            message_parts.append(line)
            i += 1

    if message_parts:
        output_blocks.append((speaker, ' '.join(message_parts)))

    # Write output
    with open(output_path, 'w', encoding='utf-8') as out:
        for sp, msg in output_blocks:
            if sp:
                out.write(f"{sp}: {msg}\n")
            else:
                out.write(f"{msg}\n")

if __name__ == '__main__':
    infile = sys.argv[1] if len(sys.argv) > 1 else 'input.txt'
    outfile = sys.argv[2] if len(sys.argv) > 2 else 'cleaned_chat.txt'
    clean_chat(infile, outfile)
    print(f"Cleaned chat saved to {outfile}")
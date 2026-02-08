def textfile_to_js_array(input_file, output_file, array_name="words"):
    try:
        # Read words from the text file
        with open(input_file, 'r', encoding='utf-8') as f:
            words = [line.strip() for line in f if line.strip()]

        # Create JavaScript array string
        js_array = f"const {array_name} = [\n"
        for word in words:
            js_array += f'  "{word}",\n'
        js_array = js_array.rstrip(",\n") + "\n];\n"

        # Write to output file
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(js_array)

        print(f"✅ Successfully created {output_file} with {len(words)} words.")
    except Exception as e:
        print(f"❌ Error: {e}")

textfile_to_js_array("words.txt", "words.js")
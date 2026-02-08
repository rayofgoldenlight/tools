const btn = document.getElementById('toggleBtn');
const term = document.getElementById('terminalOutput');
let running = false;

btn.addEventListener('click', () => {
    if (running) return;
    running = true;
    term.innerHTML = '';

    const lines = [
        "./gfind 80 110 --b 10 --t",
        "",
        "--- Block 1: 80 to 89 ---",
        "Non-redirecting URLs in block 80-89:",
        "",
        "--- Block 2: 90 to 99 ---",
        "Non-redirecting URLs in block 90-99:",
        "",
        "--- Block 3: 100 to 109 ---",
        "Non-redirecting URLs in block 100-109:",
        "  [╪¬╪▒┘ê┘è╪¼┘å╪º], https://sites.google.com/view/100",
        "  [10.2], https://sites.google.com/view/102",
        "  [ΘªûΘáü], https://sites.google.com/view/103",
        "  [ΘªûΘáü], https://sites.google.com/view/107",
        "  [math], https://sites.google.com/view/108",
        "  [ΘªûΘáü], https://sites.google.com/view/109",
        "",
        "--- Block 4: 110 to 110 ---",
        "Non-redirecting URLs in block 110-110:",
        "  [Accueil], https://sites.google.com/view/110",
        "",
        "Elapsed time: 2.853 seconds",
        "",
    ];

    let i = 0;
    const addLine = () => {
        if (i < lines.length) {
            term.innerHTML += lines[i] + "<br>";
            term.scrollTop = term.scrollHeight;
            let delay = 200;
            if (lines[i].startsWith('---')) delay = 800;
            if (lines[i].includes('https://')) delay = 300;
            if (lines[i].startsWith("Elapsed")) delay = 1000;
            i++;
            setTimeout(addLine, delay);
        } else {
            running = false;
        }
    };
    addLine();
});
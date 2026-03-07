import init from "./pkg/runtime.js";

async function start() {
    await init();
    // The #[wasm_bindgen(start)] function runs automatically during init()
}

start().catch(console.error);
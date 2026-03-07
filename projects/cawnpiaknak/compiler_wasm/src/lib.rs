use wasm_bindgen::prelude::*;
use js_sys::{Object, Reflect, Uint8Array};

#[wasm_bindgen]
pub fn init_panic_hook() {
    console_error_panic_hook::set_once();
}

/// Compile script source code into VM bytecode.
/// Returns a JS object: { bytecode: Uint8Array, variable_count: number }
#[wasm_bindgen]
pub fn compile_source_wasm(source: &str) -> Result<JsValue, JsValue> {
    let compiled = compiler::codegen::compile_source(source)
        .map_err(|e| JsValue::from_str(&e.to_string()))?;

    let bytecode = Uint8Array::from(compiled.bytecode.as_slice());

    let obj = Object::new();
    Reflect::set(&obj, &JsValue::from_str("bytecode"), &bytecode)
        .map_err(|_| JsValue::from_str("Failed to set bytecode field"))?;

    Reflect::set(
        &obj,
        &JsValue::from_str("variable_count"),
        &JsValue::from_f64(compiled.variable_count as f64),
    )
    .map_err(|_| JsValue::from_str("Failed to set variable_count field"))?;

    let behaviors_arr = js_sys::Array::new();
    for (shape_idx, code) in compiled.behaviors {
        let b_obj = Object::new();
        Reflect::set(&b_obj, &JsValue::from_str("shape_index"), &JsValue::from_f64(shape_idx as f64)).unwrap();
        Reflect::set(&b_obj, &JsValue::from_str("bytecode"), &Uint8Array::from(code.as_slice())).unwrap();
        behaviors_arr.push(&b_obj);
    }
    Reflect::set(&obj, &JsValue::from_str("behaviors"), &behaviors_arr).unwrap();

    Ok(obj.into())
}
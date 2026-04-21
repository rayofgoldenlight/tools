use compiler::codegen::compile_source;
use format::serialize::encode;
use format::*;

fn main() {
    let script = r#"
var speed = 220
var score = 0

var dx = 0
var dy = 0

var coin1 = 0
var coin2 = 0

function update(dt)
    if key_down("right") then
        obj[0].x = obj[0].x + speed * dt
    end
    if key_down("left") then
        obj[0].x = obj[0].x - speed * dt
    end
    if key_down("down") then
        obj[0].y = obj[0].y + speed * dt
    end
    if key_down("up") then
        obj[0].y = obj[0].y - speed * dt
    end

    if coin1 == 0 then
        dx = obj[0].x - obj[1].x
        dy = obj[0].y - obj[1].y

        if dx < 28 then
            if dx > -28 then
                if dy < 28 then
                    if dy > -28 then
                        score = score + 1
                        coin1 = 1
                        obj[1].x = -1000
                        obj[1].y = -1000
                    end
                end
            end
        end
    end

    if coin2 == 0 then
        dx = obj[0].x - obj[2].x
        dy = obj[0].y - obj[2].y

        if dx < 28 then
            if dx > -28 then
                if dy < 28 then
                    if dy > -28 then
                        score = score + 1
                        coin2 = 1
                        obj[2].x = -1000
                        obj[2].y = -1000
                    end
                end
            end
        end
    end

    if score == 2 then
        obj[3].x = obj[0].x + 60
        obj[3].y = obj[0].y
    end
end
"#;

    let compiled = match compile_source(script) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };

    println!("Compiled bytecode: {} bytes", compiled.bytecode.len());
    println!("VM variable_count: {}", compiled.variable_count);

    // Shapes:
    // 0 = player
    // 1 = coin
    // 2 = landmark
    let shapes = vec![
        Shape {
            shape_type: ShapeType::Circle,
            draw_mode: DrawMode::Fill,
            fill_color: Color::rgb(0, 255, 136),
            stroke_color: Color::rgb(0, 0, 0),
            stroke_width: 0.0,
            params: ShapeParams::Circle { radius: 22.0 },
        },
        Shape {
            shape_type: ShapeType::Circle,
            draw_mode: DrawMode::Fill,
            fill_color: Color::rgb(245, 230, 66),
            stroke_color: Color::rgb(0, 0, 0),
            stroke_width: 0.0,
            params: ShapeParams::Circle { radius: 12.0 },
        },
        Shape {
            shape_type: ShapeType::Rect,
            draw_mode: DrawMode::Stroke,
            fill_color: Color::rgb(0, 0, 0),
            stroke_color: Color::rgb(255, 255, 255),
            stroke_width: 2.0,
            params: ShapeParams::Rect {
                width: 48.0,
                height: 48.0,
            },
        },
    ];

    // Objects:
    // 0 = player
    // 1 = coin1
    // 2 = coin2
    // 3 = landmark (moves when score==2)
    let objects = vec![
        SceneObject {
            shape_index: 0, anim_index: 0xFFFF, anim_time: 0.0,
            x: 400.0, y: 300.0, rotation: 0.0, scale: 1.0,
            visible: true, value: 0.0, custom: [0.0, 0.0, 0.0, 0.0],
        },
        SceneObject {
            shape_index: 1, anim_index: 0xFFFF, anim_time: 0.0,
            x: 200.0, y: 200.0, rotation: 0.0, scale: 1.0,
            visible: true, value: 0.0, custom: [0.0, 0.0, 0.0, 0.0],
        },
        SceneObject {
            shape_index: 1, anim_index: 0xFFFF, anim_time: 0.0,
            x: 620.0, y: 420.0, rotation: 0.0, scale: 1.0,
            visible: true, value: 0.0, custom: [0.0, 0.0, 0.0, 0.0],
        },
        SceneObject {
            shape_index: 2, anim_index: 0xFFFF, anim_time: 0.0, 
            x: 650.0, y: 120.0, rotation: 0.0, scale: 1.0,
            visible: true, value: 0.0, custom: [0.0, 0.0, 0.0, 0.0],
        },
    ];

    let game = GameFile {
        header: Header {
            magic: MAGIC,
            version: VERSION,
            screen_width: 800,
            screen_height: 600,
            section_count: 6, // shapes, objects, bytecode, script
        },
        shapes,
        animations: vec![],
        audio: vec![],
        objects,
        bytecode: compiled.bytecode,
        behaviors: compiled.behaviors,
        variable_count: compiled.variable_count,
        script_source: script.to_string(),
    };

    let bytes = encode(&game);
    let out_path = "web/test.bin";
    std::fs::write(out_path, &bytes).expect("Failed to write test.bin");

    let out_path = "web/test.bin";
    std::fs::write(out_path, &bytes).expect("Failed to write test.bin");

    // Write base.bin for the editor
    std::fs::write("web/base.bin", &bytes).expect("Failed to write base.bin");

    println!("Wrote {} bytes to {}", bytes.len(), out_path);

    // Roundtrip verify
    let read_back = std::fs::read(out_path).expect("Failed to read");
    let parsed = format::deserialize::decode(&read_back).expect("Failed to parse");

    println!(
        "Roundtrip OK: {} shapes, {} objects, {} bytecode bytes, {} vars, {} bytes script",
        parsed.shapes.len(),
        parsed.objects.len(),
        parsed.bytecode.len(),
        parsed.variable_count,
        parsed.script_source.len(),
    );
}
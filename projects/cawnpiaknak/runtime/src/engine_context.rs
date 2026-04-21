use format::vm::{EngineContext, VMError};
use format::SceneObject;

use crate::input::InputState;

/// Real engine context that connects the VM to browser state.
pub struct RuntimeContext<'a> {
    pub input: &'a InputState,
    pub objects: &'a mut Vec<SceneObject>,
    pub dt: f32,
}

impl<'a> EngineContext for RuntimeContext<'a> {
    fn key_down(&self, key_code: u16) -> bool {
        self.input.is_key_down(key_code)
    }

    fn get_obj_x(&self, obj_id: u16) -> Result<f32, VMError> {
        self.objects
            .get(obj_id as usize)
            .map(|o| o.x)
            .ok_or(VMError::ObjectOutOfBounds(obj_id))
    }

    fn get_obj_y(&self, obj_id: u16) -> Result<f32, VMError> {
        self.objects
            .get(obj_id as usize)
            .map(|o| o.y)
            .ok_or(VMError::ObjectOutOfBounds(obj_id))
    }

    fn set_obj_x(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> {
        self.objects
            .get_mut(obj_id as usize)
            .map(|o| o.x = val)
            .ok_or(VMError::ObjectOutOfBounds(obj_id))
    }

    fn set_obj_y(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> {
        self.objects
            .get_mut(obj_id as usize)
            .map(|o| o.y = val)
            .ok_or(VMError::ObjectOutOfBounds(obj_id))
    }

    fn play_anim(&mut self, obj_id: u16, anim_id: u16) -> Result<(), VMError> {
        let obj = self.objects.get_mut(obj_id as usize).ok_or(VMError::ObjectOutOfBounds(obj_id))?;
        // Only reset the timer if we are switching to a different animation
        if obj.anim_index != anim_id {
            obj.anim_index = anim_id;
            obj.anim_time = 0.0;
        }
        Ok(())
    }

    fn play_sound(&mut self, sound_id: u16) -> Result<(), VMError> {
        crate::play_audio_js(sound_id);
        Ok(())
    }

    fn stop_sound(&mut self, sound_id: u16) -> Result<(), VMError> {
        crate::stop_audio_js(sound_id);
        Ok(())
    }

    fn loop_sound(&mut self, sound_id: u16) -> Result<(), VMError> {
        crate::loop_audio_js(sound_id);
        Ok(())
    }
    fn set_volume(&mut self, sound_id: u16, vol: f32) -> Result<(), VMError> {
        crate::set_volume_js(sound_id, vol);
        Ok(())
    }

    fn get_obj_visible(&self, obj_id: u16) -> Result<f32, VMError> {
        self.objects.get(obj_id as usize).map(|o| if o.visible { 1.0 } else { 0.0 }).ok_or(VMError::ObjectOutOfBounds(obj_id))
    }
    fn set_obj_visible(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> {
        let obj = self.objects.get_mut(obj_id as usize).ok_or(VMError::ObjectOutOfBounds(obj_id))?;
        obj.visible = val != 0.0; Ok(())
    }
    fn get_obj_value(&self, obj_id: u16) -> Result<f32, VMError> {
        self.objects.get(obj_id as usize).map(|o| o.value).ok_or(VMError::ObjectOutOfBounds(obj_id))
    }
    fn set_obj_value(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> {
        let obj = self.objects.get_mut(obj_id as usize).ok_or(VMError::ObjectOutOfBounds(obj_id))?;
        obj.value = val; Ok(())
    }

    fn mouse_x(&self) -> f32 { self.input.mouse_x() }
    fn mouse_y(&self) -> f32 { self.input.mouse_y() }
    fn mouse_down(&self) -> bool { self.input.mouse_down() }

    fn get_obj_custom(&self, obj_id: u16, slot: u8) -> Result<f32, VMError> {
        if slot >= 4 { return Ok(0.0); } // Prevent WASM panic
        self.objects.get(obj_id as usize)
            .map(|o| o.custom[slot as usize])
            .ok_or(VMError::ObjectOutOfBounds(obj_id))
    }

    fn set_obj_custom(&mut self, obj_id: u16, slot: u8, val: f32) -> Result<(), VMError> {
        if slot >= 4 { return Ok(()); } // Prevent WASM panic
        let obj = self.objects.get_mut(obj_id as usize)
            .ok_or(VMError::ObjectOutOfBounds(obj_id))?;
        obj.custom[slot as usize] = val; 
        Ok(())
    }

    fn get_obj_rotation(&self, obj_id: u16) -> Result<f32, VMError> {
        self.objects.get(obj_id as usize).map(|o| o.rotation).ok_or(VMError::ObjectOutOfBounds(obj_id))
    }
    fn set_obj_rotation(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> {
        let obj = self.objects.get_mut(obj_id as usize).ok_or(VMError::ObjectOutOfBounds(obj_id))?;
        obj.rotation = val; Ok(())
    }

    fn get_obj_scale(&self, obj_id: u16) -> Result<f32, VMError> {
        self.objects.get(obj_id as usize).map(|o| o.scale).ok_or(VMError::ObjectOutOfBounds(obj_id))
    }
    fn set_obj_scale(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> {
        let obj = self.objects.get_mut(obj_id as usize).ok_or(VMError::ObjectOutOfBounds(obj_id))?;
        obj.scale = val; Ok(())
    }

    fn get_dt(&self) -> f32 {
        self.dt
    }
}
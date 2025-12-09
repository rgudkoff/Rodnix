// Common types for userspace

pub type DeviceId = u32;
pub type Capability = u64;

#[repr(C)]
pub struct Device {
    pub id: DeviceId,
    pub name: [u8; 32],
    pub device_type: u32,
}

#[repr(C)]
pub struct IpcMessage {
    pub from: u32,
    pub to: u32,
    pub data: [u8; 256],
    pub len: usize,
}


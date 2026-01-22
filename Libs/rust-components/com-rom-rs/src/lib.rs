use std::ffi::{CStr, c_void};
use std::sync::atomic::{AtomicU32, Ordering};

// ============================================================================
// COM Interface Definitions  (UNTOUCHED)
// ============================================================================

pub const IID_IDACOMPONENT: &[u8] = b"IDAComponent";
pub const IID_ICOMPONENT_FACTORY: &[u8] = b"IComponentFactory";

#[repr(C)]
pub struct IDAConnectionPoint {
    _unused: *mut c_void,
}

#[repr(C)]
pub struct DACOMDESC {
    pub size: u32,
    pub interface_name: *const u8,
}

#[repr(i32)]
#[derive(Clone, Copy)]
pub enum GENRESULT {
    Ok = 0,
    Generic = -1,
    InvalidParams = -2,
    InterfaceUnsupported = -3,
    OutOfMemory = -4,
    OutOfSpace = -5,
    FileError = -6,
    NotImplemented = -7,
    DataNotFound = -8,
}

pub type BOOL32 = i32;

pub type DACOMENUMCALLBACK =
    extern "C" fn(*mut IComponentFactory, *const u8, u32, *mut c_void) -> BOOL32;

pub const DACOM_HIGH_PRIORITY: u32 = 0xC0000000;
pub const DACOM_NORMAL_PRIORITY: u32 = 0x80000000;
pub const DACOM_LOW_PRIORITY: u32 = 0x40000000;

// ============================================================================
// VTABLES (UNTOUCHED LAYOUT)
// ============================================================================

#[repr(C)]
#[allow(non_snake_case)]
pub struct IDAComponentVTable {
    pub QueryInterface: extern "system" fn(*mut RustComponent, *const u8, *mut *mut c_void) -> i32,
    pub AddRef: extern "system" fn(*mut RustComponent) -> u32,
    pub Release: extern "system" fn(*mut RustComponent) -> u32,
    pub QueryOutgoingInterface:
        extern "system" fn(*mut RustComponent, *const u8, *mut *mut IDAConnectionPoint) -> i32,
}

#[repr(C)]
#[allow(non_snake_case)]
pub struct IComponentFactoryVTable {
    pub QueryInterface: extern "system" fn(*mut RustComponent, *const u8, *mut *mut c_void) -> i32,
    pub AddRef: extern "system" fn(*mut RustComponent) -> u32,
    pub Release: extern "system" fn(*mut RustComponent) -> u32,
    pub QueryOutgoingInterface:
        extern "system" fn(*mut RustComponent, *const u8, *mut *mut IDAConnectionPoint) -> i32,
    pub CreateInstance:
        extern "system" fn(*mut RustComponent, *mut DACOMDESC, *mut *mut c_void) -> i32,
}

#[repr(C)]
pub struct IComponentFactory {
    vtable: *const IComponentFactoryVTable,
}

#[repr(C)]
#[allow(non_snake_case)]
pub struct ICOManagerVTable {
    pub QueryInterface: extern "system" fn(*mut RustComponent, *const u8, *mut *mut c_void) -> i32,
    pub AddRef: extern "system" fn(*mut RustComponent) -> u32,
    pub Release: extern "system" fn(*mut RustComponent) -> u32,
    pub QueryOutgoingInterface:
        extern "system" fn(*mut RustComponent, *const u8, *mut *mut IDAConnectionPoint) -> i32,
    pub CreateInstance:
        extern "system" fn(*mut RustComponent, *mut DACOMDESC, *mut *mut c_void) -> i32,
    pub RegisterComponent:
        extern "system" fn(*mut RustComponent, *mut IComponentFactory, *const u8, u32) -> i32,
    pub UnregisterComponent:
        extern "system" fn(*mut RustComponent, *mut IComponentFactory, *const u8) -> i32,
    pub EnumerateComponents:
        extern "system" fn(*mut RustComponent, *const u8, DACOMENUMCALLBACK, *mut c_void) -> i32,
    pub AddLibrary: extern "system" fn(*mut RustComponent, *const u8) -> i32,
    pub RemoveLibrary: extern "system" fn(*mut RustComponent, *const u8) -> i32,
    pub ShutDown: extern "system" fn(*mut RustComponent) -> i32,
    pub SetINIConfig: extern "system" fn(*mut RustComponent, *const u8, u32) -> i32,
}

#[repr(C)]
#[allow(non_snake_case)]
pub struct IRustFunnyComponentVTable {
    pub QueryInterface: extern "system" fn(*mut RustComponent, *const u8, *mut *mut c_void) -> i32,
    pub AddRef: extern "system" fn(*mut RustComponent) -> u32,
    pub Release: extern "system" fn(*mut RustComponent) -> u32,
    pub QueryOutgoingInterface:
        extern "system" fn(*mut RustComponent, *const u8, *mut *mut IDAConnectionPoint) -> i32,
    pub GetTheFunnyNumber: extern "system" fn(*mut RustComponent) -> i32,
    pub ConcatenateStringWithInt:
        extern "system" fn(*mut RustComponent, *const u8, i32, *mut u8, u32) -> i32,
}

// ============================================================================
// COMPONENT CORE
// ============================================================================

#[repr(u32)]
#[derive(Clone, Copy)]
enum ComponentType {
    DAComponent,
    ComponentFactory,
}

#[repr(C)]
pub struct RustComponent {
    vtable: *const c_void,
    ref_count: *mut AtomicU32,
    ty: ComponentType,
}

impl RustComponent {
    fn new(vtable: *const c_void, ty: ComponentType) -> Box<Self> {
        Box::new(Self {
            vtable,
            ref_count: Box::into_raw(Box::new(AtomicU32::new(1))),
            ty,
        })
    }

    fn add_ref(&self) -> u32 {
        unsafe { (*self.ref_count).fetch_add(1, Ordering::SeqCst) + 1 }
    }

    fn release(&mut self) -> u32 {
        unsafe {
            let count = (*self.ref_count).fetch_sub(1, Ordering::SeqCst) - 1;
            if count == 0 {
                let _ = Box::from_raw(self.ref_count);
                let _ = Box::from_raw(self);
            }
            count
        }
    }

    fn query_interface(&self, iid: *const u8, out: *mut *mut c_void) -> i32 {
        if iid.is_null() || out.is_null() {
            return GENRESULT::InvalidParams as i32;
        }

        let iid = unsafe { CStr::from_ptr(iid as *const i8).to_bytes() };

        let supported = match self.ty {
            ComponentType::DAComponent => iid == IID_IDACOMPONENT,
            ComponentType::ComponentFactory => {
                iid == IID_IDACOMPONENT || iid == IID_ICOMPONENT_FACTORY
            }
        };

        unsafe {
            if supported {
                *out = self as *const _ as *mut _;
                self.add_ref();
                GENRESULT::Ok as i32
            } else {
                *out = std::ptr::null_mut();
                GENRESULT::InterfaceUnsupported as i32
            }
        }
    }
}

// ============================================================================
// SHARED EXTERN IMPLEMENTATIONS
// ============================================================================

extern "system" fn qi(this: *mut RustComponent, iid: *const u8, out: *mut *mut c_void) -> i32 {
    unsafe {
        this.as_ref()
            .map_or(GENRESULT::Generic as i32, |c| c.query_interface(iid, out))
    }
}

extern "system" fn addref(this: *mut RustComponent) -> u32 {
    unsafe { this.as_ref().map_or(0, |c| c.add_ref()) }
}

extern "system" fn release(this: *mut RustComponent) -> u32 {
    unsafe { this.as_mut().map_or(0, |c| c.release()) }
}

extern "system" fn qoi(
    _: *mut RustComponent,
    _: *const u8,
    _: *mut *mut IDAConnectionPoint,
) -> i32 {
    GENRESULT::NotImplemented as i32
}

// ============================================================================
// FACTORY / FUNNY IMPLEMENTATIONS
// ============================================================================

pub const IID_IRUST_FUNNY_COMPONENT: &[u8] = b"IRustFunnyComponent";

extern "system" fn create_instance(
    _: *mut RustComponent,
    desc: *mut DACOMDESC,
    out: *mut *mut c_void,
) -> i32 {
    if desc.is_null() || out.is_null() {
        return GENRESULT::InvalidParams as i32;
    }

    let iface = unsafe { CStr::from_ptr((*desc).interface_name as *const i8).to_bytes() };

    if iface != IID_IRUST_FUNNY_COMPONENT {
        return GENRESULT::InterfaceUnsupported as i32;
    }

    let obj = RustComponent::new(
        &FUNNY_COMPONENT_VTABLE as *const _ as *const c_void,
        ComponentType::DAComponent,
    );

    unsafe { *out = Box::into_raw(obj) as *mut _ };
    GENRESULT::Ok as i32
}

extern "system" fn funny_number(_: *mut RustComponent) -> i32 {
    69
}

extern "system" fn funny_concat(
    _: *mut RustComponent,
    s: *const u8,
    n: i32,
    out: *mut u8,
    cap: u32,
) -> i32 {
    if s.is_null() || out.is_null() {
        return GENRESULT::InvalidParams as i32;
    }

    let s = unsafe { CStr::from_ptr(s as *const i8).to_string_lossy() };
    let buf = format!("{} {}", s, n);
    let bytes = buf.as_bytes();

    if bytes.len() + 1 > cap as usize {
        return GENRESULT::OutOfSpace as i32;
    }

    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, bytes.len());
        *out.add(bytes.len()) = 0;
    }

    GENRESULT::Ok as i32
}

// ============================================================================
// VTABLE INSTANCES (FLAT, ABI-SAFE)
// ============================================================================

static DA_COMPONENT_VTABLE: IDAComponentVTable = IDAComponentVTable {
    QueryInterface: qi,
    AddRef: addref,
    Release: release,
    QueryOutgoingInterface: qoi,
};

static COMPONENT_FACTORY_VTABLE: IComponentFactoryVTable = IComponentFactoryVTable {
    QueryInterface: qi,
    AddRef: addref,
    Release: release,
    QueryOutgoingInterface: qoi,
    CreateInstance: create_instance,
};

static FUNNY_COMPONENT_VTABLE: IRustFunnyComponentVTable = IRustFunnyComponentVTable {
    QueryInterface: qi,
    AddRef: addref,
    Release: release,
    QueryOutgoingInterface: qoi,
    GetTheFunnyNumber: funny_number,
    ConcatenateStringWithInt: funny_concat,
};

// ============================================================================
// DLL ENTRY
// ============================================================================

unsafe extern "system" {
    fn DACOM_Acquire() -> *mut RustComponent;
}
const DLL_PROCESS_ATTACH: u32 = 1;
const DLL_PROCESS_DETACH: u32 = 0;
#[unsafe(no_mangle)]
#[allow(non_snake_case)]
pub extern "system" fn DllMain(_: *mut c_void, reason: u32, _: *mut c_void) -> i32 {
    if reason == DLL_PROCESS_ATTACH {
        unsafe {
            let factory = Box::into_raw(RustComponent::new(
                &COMPONENT_FACTORY_VTABLE as *const _ as *const c_void,
                ComponentType::ComponentFactory,
            ));

            let dacom = DACOM_Acquire();
            if !dacom.is_null() {
                let vt = (*dacom).vtable as *const ICOManagerVTable;
                ((*vt).RegisterComponent)(
                    dacom,
                    factory as *mut _,
                    IID_IRUST_FUNNY_COMPONENT.as_ptr(),
                    DACOM_NORMAL_PRIORITY,
                );
                ((*vt).Release)(dacom);
            }
        }
    }
    1
}

// ============================================================================
// DLL EXPORTS
// ============================================================================

#[unsafe(no_mangle)]
#[allow(non_snake_case)]
pub extern "system" fn DllCanUnloadNow() -> i32 {
    0
}

#[unsafe(no_mangle)]
#[allow(non_snake_case)]
    pub extern "system" fn DllRegisterServer() -> i32 {
    0
}

#[unsafe(no_mangle)]
#[allow(non_snake_case)]
pub extern "system" fn DllUnregisterServer() -> i32 {
    0
}

use libc::{RTLD_GLOBAL, RTLD_NOW};
use opensips_mod_sys::sys::*;
use std::ptr::null_mut;

#[no_mangle]
#[used]
pub static exports: module_exports = module_exports {
    name: b"sample\0" as *const u8 as *const i8 as *mut i8,
    type_: module_type_MOD_TYPE_DEFAULT,
    version: OPENSIPS_FULL_VERSION.as_ptr() as *const i8 as *mut i8,
    compile_flags: OPENSIPS_COMPILE_FLAGS.as_ptr() as *const i8 as *mut i8,
    dlflags: (RTLD_NOW | RTLD_GLOBAL) as u32,
    load_f: None,
    deps: null_mut(),
    cmds: [
        cmd_export_ {
            name: b"sample_hello\0" as *const u8 as *const i8 as *mut i8,
            function: Some(sample_hello),
            param_no: 0,
            fixup: None,
            free_fixup: None,
            flags: (REQUEST_ROUTE | FAILURE_ROUTE | ONREPLY_ROUTE | BRANCH_ROUTE) as i32,
        },
        cmd_export_t {
            name: null_mut(),
            function: None,
            param_no: 0,
            fixup: None,
            free_fixup: None,
            flags: 0,
        },
    ]
    .as_ptr() as *const _ as *mut _,
    acmds: null_mut(),
    params: null_mut(),
    stats: null_mut(),
    mi_cmds: null_mut(),
    items: null_mut(),
    trans: null_mut(),
    procs: null_mut(),
    preinit_f: None,
    init_f: Some(mod_init),
    response_f: None,
    destroy_f: None,
    init_child_f: None,
};

unsafe extern "C" fn sample_hello(
    arg1: *mut sip_msg,
    _arg2: *mut ::std::os::raw::c_char,
    _arg3: *mut ::std::os::raw::c_char,
    _arg4: *mut ::std::os::raw::c_char,
    _arg5: *mut ::std::os::raw::c_char,
    _arg6: *mut ::std::os::raw::c_char,
    _arg7: *mut ::std::os::raw::c_char,
) -> ::std::os::raw::c_int {
    let Some(arg1) = arg1.as_mut() else {
        eprintln!("Null sip msg");
        return 0;
    };

    match arg1.first_line.type_ {
        1 => {
            let method = get_str(&arg1.first_line.u.request.method);
            let uri = get_str(&arg1.first_line.u.request.uri);
            let version = get_str(&arg1.first_line.u.request.version);

            eprintln!("REQ: {method:?} {uri:?} {version:?}");
        }
        2 => {
            let version = get_str(&arg1.first_line.u.reply.version);
            let status = get_str(&arg1.first_line.u.reply.status);
            let reason = get_str(&arg1.first_line.u.reply.reason);

            eprintln!("RES: {version:?} {status:?} {reason:?}");
        }
        x => {
            eprintln!("Invalid first line: {x}");
        }
    }

    eprintln!("{:?}", arg1.body);
    if let Some(body) = arg1.body.as_ref() {
        eprintln!("Body: {:?}", get_str(&body.body));
    }

    eprintln!("{buf:?} {len:?}", buf = arg1.buf, len = arg1.len);

    if arg1.buf.is_null() {
        return 0;
    }

    let buf = std::slice::from_raw_parts(arg1.buf as *const u8, arg1.len as usize);
    let buf = String::from_utf8_lossy(buf);

    eprintln!("Buf {buf:?}");

    0
}

unsafe extern "C" fn mod_init() -> ::std::os::raw::c_int {
    eprintln!("Loaded rust code!");
    0
}

unsafe fn get_str(string: &str_) -> String {
    String::from_utf8_lossy(std::slice::from_raw_parts(
        string.s as *mut u8,
        string.len as usize,
    ))
    .into_owned()
}

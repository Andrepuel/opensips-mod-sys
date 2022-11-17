#[allow(
    clippy::missing_safety_doc,
    non_camel_case_types,
    non_upper_case_globals,
)]
pub mod sys;

unsafe impl Sync for sys::module_exports {}
unsafe impl Send for sys::module_exports {}
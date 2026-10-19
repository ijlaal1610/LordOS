fn main() {
    // `windows`: writing a whole disk needs an administrator, so the app asks for it when it starts
    // (src-tauri/windows-app.manifest). Mac and Linux ask only when the drive is opened.
    #[allow(unused_mut)]
    let mut attrs = tauri_build::Attributes::new();
    #[cfg(windows)]
    {
        attrs = attrs.windows_attributes(tauri_build::WindowsAttributes::new().app_manifest(include_str!("windows-app.manifest")));
    }
    tauri_build::try_build(attrs).expect("tauri-build");
}

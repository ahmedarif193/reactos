#![windows_subsystem = "windows"]
#![allow(non_snake_case)]
#![allow(dead_code)]

use core::ffi::c_void;
use std::ptr::{null, null_mut};
use std::fs::{File, remove_file};
use std::io::{Write as IoWrite, Read};
use std::time::Instant;
use std::cell::RefCell;

type HINSTANCE = *mut c_void;
type HWND = *mut c_void;
type HICON = *mut c_void;
type HCURSOR = *mut c_void;
type HBRUSH = *mut c_void;
type HDC = *mut c_void;
type HMENU = *mut c_void;
type HFONT = *mut c_void;

type LRESULT = isize;
type WPARAM = usize;
type LPARAM = isize;
type BOOL = i32;
type UINT = u32;
type DWORD = u32;
type ATOM = u16;
type WORD = u16;
type LONG = i32;
type ULONGLONG = u64;

const CS_VREDRAW: UINT = 0x0001;
const CS_HREDRAW: UINT = 0x0002;
const CW_USEDEFAULT: i32 = 0x8000_0000u32 as i32;
const WS_OVERLAPPEDWINDOW: DWORD = 0x00CF_0000;
const WS_VISIBLE: DWORD = 0x10000000;
const WS_CHILD: DWORD = 0x40000000;
const WS_TABSTOP: DWORD = 0x00010000;
const BS_PUSHBUTTON: DWORD = 0x00000000;
const BS_GROUPBOX: DWORD = 0x00000007;
const SS_LEFT: DWORD = 0x00000000;
const SW_SHOW: i32 = 5;

const WM_DESTROY: UINT = 0x0002;
const WM_PAINT: UINT = 0x000F;
const WM_COMMAND: UINT = 0x0111;
const WM_CREATE: UINT = 0x0001;
const WM_CTLCOLORSTATIC: UINT = 0x0138;

const IDC_ARROW: *const u16 = 32512usize as *const u16;
const IDI_APPLICATION: *const u16 = 32512usize as *const u16;
const COLOR_WINDOW: usize = 5;
const COLOR_BTNFACE: usize = 15;

const IDC_BUTTON_BENCH_C: i32 = 101;
const IDC_BUTTON_BENCH_D: i32 = 102;
const IDC_BUTTON_REFRESH: i32 = 103;

const FW_NORMAL: i32 = 400;
const FW_BOLD: i32 = 700;
const DEFAULT_CHARSET: DWORD = 1;
const OUT_DEFAULT_PRECIS: DWORD = 0;
const CLIP_DEFAULT_PRECIS: DWORD = 0;
const DEFAULT_QUALITY: DWORD = 0;
const FF_DONTCARE: DWORD = 0;

const GMEM_FIXED: UINT = 0x0000;

#[repr(C)]
struct WNDCLASSW {
    style: UINT,
    lpfnWndProc: Option<unsafe extern "system" fn(HWND, UINT, WPARAM, LPARAM) -> LRESULT>,
    cbClsExtra: i32,
    cbWndExtra: i32,
    hInstance: HINSTANCE,
    hIcon: HICON,
    hCursor: HCURSOR,
    hbrBackground: HBRUSH,
    lpszMenuName: *const u16,
    lpszClassName: *const u16,
}

#[repr(C)]
struct POINT {
    x: i32,
    y: i32,
}

#[repr(C)]
struct MSG {
    hwnd: HWND,
    message: UINT,
    wParam: WPARAM,
    lParam: LPARAM,
    time: DWORD,
    pt: POINT,
}

#[repr(C)]
struct RECT {
    left: i32,
    top: i32,
    right: i32,
    bottom: i32,
}

#[repr(C)]
struct PAINTSTRUCT {
    hdc: HDC,
    fErase: BOOL,
    rcPaint: RECT,
    fRestore: BOOL,
    fIncUpdate: BOOL,
    rgbReserved: [u8; 32],
}

#[repr(C)]
struct SYSTEM_INFO {
    wProcessorArchitecture: WORD,
    wReserved: WORD,
    dwPageSize: DWORD,
    lpMinimumApplicationAddress: *mut c_void,
    lpMaximumApplicationAddress: *mut c_void,
    dwActiveProcessorMask: usize,
    dwNumberOfProcessors: DWORD,
    dwProcessorType: DWORD,
    dwAllocationGranularity: DWORD,
    wProcessorLevel: WORD,
    wProcessorRevision: WORD,
}

#[repr(C)]
struct MEMORYSTATUSEX {
    dwLength: DWORD,
    dwMemoryLoad: DWORD,
    ullTotalPhys: ULONGLONG,
    ullAvailPhys: ULONGLONG,
    ullTotalPageFile: ULONGLONG,
    ullAvailPageFile: ULONGLONG,
    ullTotalVirtual: ULONGLONG,
    ullAvailVirtual: ULONGLONG,
    ullAvailExtendedVirtual: ULONGLONG,
}

#[repr(C)]
struct OSVERSIONINFOEXW {
    dwOSVersionInfoSize: DWORD,
    dwMajorVersion: DWORD,
    dwMinorVersion: DWORD,
    dwBuildNumber: DWORD,
    dwPlatformId: DWORD,
    szCSDVersion: [u16; 128],
    wServicePackMajor: WORD,
    wServicePackMinor: WORD,
    wSuiteMask: WORD,
    wProductType: u8,
    wReserved: u8,
}

#[link(name = "user32")]
extern "system" {
    fn RegisterClassW(lpWndClass: *const WNDCLASSW) -> ATOM;
    fn CreateWindowExW(
        dwExStyle: DWORD, lpClassName: *const u16, lpWindowName: *const u16,
        dwStyle: DWORD, X: i32, Y: i32, nWidth: i32, nHeight: i32,
        hWndParent: HWND, hMenu: HMENU, hInstance: HINSTANCE, lpParam: *mut c_void,
    ) -> HWND;
    fn DefWindowProcW(hWnd: HWND, Msg: UINT, wParam: WPARAM, lParam: LPARAM) -> LRESULT;
    fn ShowWindow(hWnd: HWND, nCmdShow: i32) -> BOOL;
    fn UpdateWindow(hWnd: HWND) -> BOOL;
    fn GetMessageW(lpMsg: *mut MSG, hWnd: HWND, wMsgFilterMin: UINT, wMsgFilterMax: UINT) -> i32;
    fn TranslateMessage(lpMsg: *const MSG) -> BOOL;
    fn DispatchMessageW(lpMsg: *const MSG) -> LRESULT;
    fn PostQuitMessage(nExitCode: i32);
    fn LoadIconW(hInstance: HINSTANCE, lpIconName: *const u16) -> HICON;
    fn LoadCursorW(hInstance: HINSTANCE, lpCursorName: *const u16) -> HCURSOR;
    fn SetWindowTextW(hWnd: HWND, lpString: *const u16) -> BOOL;
    fn InvalidateRect(hWnd: HWND, lpRect: *const RECT, bErase: BOOL) -> BOOL;
    fn GetDC(hWnd: HWND) -> HDC;
    fn ReleaseDC(hWnd: HWND, hDC: HDC) -> i32;
}

#[link(name = "gdi32")]
extern "system" {
    fn BeginPaint(hWnd: HWND, lpPaint: *mut PAINTSTRUCT) -> HDC;
    fn EndPaint(hWnd: HWND, lpPaint: *const PAINTSTRUCT) -> BOOL;
    fn TextOutW(hdc: HDC, x: i32, y: i32, lpString: *const u16, c: i32) -> i32;
    fn CreateFontW(
        cHeight: i32, cWidth: i32, cEscapement: i32, cOrientation: i32,
        cWeight: i32, bItalic: DWORD, bUnderline: DWORD, bStrikeOut: DWORD,
        iCharSet: DWORD, iOutPrecision: DWORD, iClipPrecision: DWORD,
        iQuality: DWORD, iPitchAndFamily: DWORD, pszFaceName: *const u16,
    ) -> HFONT;
    fn SelectObject(hdc: HDC, h: *mut c_void) -> *mut c_void;
    fn SetBkMode(hdc: HDC, mode: i32) -> i32;
    fn SetTextColor(hdc: HDC, color: DWORD) -> DWORD;
}

#[link(name = "kernel32")]
extern "system" {
    fn GetModuleHandleW(lpModuleName: *const u16) -> HINSTANCE;
    fn GetSystemInfo(lpSystemInfo: *mut SYSTEM_INFO);
    fn GlobalMemoryStatusEx(lpBuffer: *mut MEMORYSTATUSEX) -> BOOL;
    fn GetVersionExW(lpVersionInfo: *mut OSVERSIONINFOEXW) -> BOOL;
    fn GetLogicalDrives() -> DWORD;
    fn GetDiskFreeSpaceExW(
        lpDirectoryName: *const u16,
        lpFreeBytesAvailableToCaller: *mut ULONGLONG,
        lpTotalNumberOfBytes: *mut ULONGLONG,
        lpTotalNumberOfFreeBytes: *mut ULONGLONG,
    ) -> BOOL;
    fn GetVolumeInformationW(
        lpRootPathName: *const u16,
        lpVolumeNameBuffer: *mut u16,
        nVolumeNameSize: DWORD,
        lpVolumeSerialNumber: *mut DWORD,
        lpMaximumComponentLength: *mut DWORD,
        lpFileSystemFlags: *mut DWORD,
        lpFileSystemNameBuffer: *mut u16,
        nFileSystemNameSize: DWORD,
    ) -> BOOL;
}

struct SystemInfo {
    cpu_arch: String,
    num_processors: u32,
    page_size: u32,
    total_memory_mb: u64,
    avail_memory_mb: u64,
    os_version: String,
    benchmark_results: RefCell<Vec<(String, f64, f64)>>, // (drive, read_speed, write_speed)
}

impl SystemInfo {
    fn new() -> Self {
        let mut sysinfo = SYSTEM_INFO {
            wProcessorArchitecture: 0,
            wReserved: 0,
            dwPageSize: 0,
            lpMinimumApplicationAddress: null_mut(),
            lpMaximumApplicationAddress: null_mut(),
            dwActiveProcessorMask: 0,
            dwNumberOfProcessors: 0,
            dwProcessorType: 0,
            dwAllocationGranularity: 0,
            wProcessorLevel: 0,
            wProcessorRevision: 0,
        };

        unsafe { GetSystemInfo(&mut sysinfo) };

        let cpu_arch = match sysinfo.wProcessorArchitecture {
            0 => "x86 (Intel/AMD 32-bit)",
            5 => "ARM",
            6 => "Intel Itanium",
            9 => "x64 (AMD64/Intel64)",
            12 => "ARM64",
            _ => "Unknown",
        }.to_string();

        let mut memstat = MEMORYSTATUSEX {
            dwLength: std::mem::size_of::<MEMORYSTATUSEX>() as DWORD,
            dwMemoryLoad: 0,
            ullTotalPhys: 0,
            ullAvailPhys: 0,
            ullTotalPageFile: 0,
            ullAvailPageFile: 0,
            ullTotalVirtual: 0,
            ullAvailVirtual: 0,
            ullAvailExtendedVirtual: 0,
        };

        unsafe { GlobalMemoryStatusEx(&mut memstat) };

        let mut osver = OSVERSIONINFOEXW {
            dwOSVersionInfoSize: std::mem::size_of::<OSVERSIONINFOEXW>() as DWORD,
            dwMajorVersion: 0,
            dwMinorVersion: 0,
            dwBuildNumber: 0,
            dwPlatformId: 0,
            szCSDVersion: [0; 128],
            wServicePackMajor: 0,
            wServicePackMinor: 0,
            wSuiteMask: 0,
            wProductType: 0,
            wReserved: 0,
        };

        let os_version = unsafe {
            if GetVersionExW(&mut osver) != 0 {
                format!("ReactOS {}.{} Build {}", osver.dwMajorVersion, osver.dwMinorVersion, osver.dwBuildNumber)
            } else {
                "ReactOS (Unknown Version)".to_string()
            }
        };

        SystemInfo {
            cpu_arch,
            num_processors: sysinfo.dwNumberOfProcessors,
            page_size: sysinfo.dwPageSize,
            total_memory_mb: memstat.ullTotalPhys / (1024 * 1024),
            avail_memory_mb: memstat.ullAvailPhys / (1024 * 1024),
            os_version,
            benchmark_results: RefCell::new(Vec::new()),
        }
    }

    fn get_logical_drives() -> Vec<char> {
        let mut drives = Vec::new();
        unsafe {
            let bitmask = GetLogicalDrives();
            for i in 0..26 {
                if (bitmask & (1 << i)) != 0 {
                    drives.push((b'A' + i as u8) as char);
                }
            }
        }
        drives
    }

    fn benchmark_drive(&self, drive: char) -> (f64, f64) {
        const TEST_SIZE_MB: usize = 50;
        const BLOCK_SIZE: usize = 1024 * 1024;

        let test_file = format!("{}:\\benchmark.tmp", drive);

        // Write benchmark
        let write_speed = match File::create(&test_file) {
            Ok(mut file) => {
                let buffer = vec![0xAA_u8; BLOCK_SIZE];
                let start = Instant::now();

                for _ in 0..TEST_SIZE_MB {
                    let _ = file.write_all(&buffer);
                }
                let _ = file.sync_all();

                let elapsed = start.elapsed();
                TEST_SIZE_MB as f64 / elapsed.as_secs_f64()
            }
            Err(_) => 0.0,
        };

        // Read benchmark
        let read_speed = match File::open(&test_file) {
            Ok(mut file) => {
                let mut buffer = vec![0_u8; BLOCK_SIZE];
                let start = Instant::now();

                for _ in 0..TEST_SIZE_MB {
                    let _ = file.read_exact(&mut buffer);
                }

                let elapsed = start.elapsed();
                TEST_SIZE_MB as f64 / elapsed.as_secs_f64()
            }
            Err(_) => 0.0,
        };

        let _ = remove_file(&test_file);

        (read_speed, write_speed)
    }
}

static mut G_SYSINFO: Option<SystemInfo> = None;
static mut G_HWND_MAIN: HWND = null_mut();

unsafe extern "system" fn wndproc(hwnd: HWND, msg: UINT, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match msg {
        WM_CREATE => {
            G_HWND_MAIN = hwnd;
            G_SYSINFO = Some(SystemInfo::new());

            let hinstance = GetModuleHandleW(null());

            // Create system info labels
            let mut y_pos = 15;
            let x_label = 20;
            let x_value = 200;

            if let Some(ref sysinfo) = G_SYSINFO {
                create_label(hwnd, hinstance, "SYSTEM INFORMATION", x_label, y_pos, 760, 20, true);
                y_pos += 30;

                create_label(hwnd, hinstance, &format!("CPU Architecture:"), x_label, y_pos, 170, 18, false);
                create_label(hwnd, hinstance, &sysinfo.cpu_arch, x_value, y_pos, 380, 18, false);
                y_pos += 22;

                create_label(hwnd, hinstance, &format!("Number of Processors:"), x_label, y_pos, 170, 18, false);
                create_label(hwnd, hinstance, &format!("{} CPU(s)", sysinfo.num_processors), x_value, y_pos, 380, 18, false);
                y_pos += 22;

                create_label(hwnd, hinstance, &format!("Page Size:"), x_label, y_pos, 170, 18, false);
                create_label(hwnd, hinstance, &format!("{} bytes", sysinfo.page_size), x_value, y_pos, 380, 18, false);
                y_pos += 22;

                create_label(hwnd, hinstance, &format!("Total Physical Memory:"), x_label, y_pos, 170, 18, false);
                create_label(hwnd, hinstance, &format!("{} MB ({:.2} GB)", sysinfo.total_memory_mb, sysinfo.total_memory_mb as f64 / 1024.0), x_value, y_pos, 380, 18, false);
                y_pos += 22;

                create_label(hwnd, hinstance, &format!("Available Memory:"), x_label, y_pos, 170, 18, false);
                create_label(hwnd, hinstance, &format!("{} MB ({:.2} GB)", sysinfo.avail_memory_mb, sysinfo.avail_memory_mb as f64 / 1024.0), x_value, y_pos, 380, 18, false);
                y_pos += 22;

                create_label(hwnd, hinstance, &format!("Operating System:"), x_label, y_pos, 170, 18, false);
                create_label(hwnd, hinstance, &sysinfo.os_version, x_value, y_pos, 380, 18, false);
                y_pos += 35;

                // Disk info section
                create_label(hwnd, hinstance, "DISK INFORMATION", x_label, y_pos, 760, 20, true);
                y_pos += 30;

                let drives = SystemInfo::get_logical_drives();
                for drive in &drives {
                    let drive_path = format!("{}:\\", drive);
                    let drive_path_w = to_wstring(&drive_path);

                    let mut total: ULONGLONG = 0;
                    let mut free: ULONGLONG = 0;
                    let mut avail: ULONGLONG = 0;
                    let mut fs_name = [0u16; 32];

                    if GetDiskFreeSpaceExW(drive_path_w.as_ptr(), &mut avail, &mut total, &mut free) != 0 {
                        GetVolumeInformationW(
                            drive_path_w.as_ptr(),
                            null_mut(),
                            0,
                            null_mut(),
                            null_mut(),
                            null_mut(),
                            fs_name.as_mut_ptr(),
                            32,
                        );

                        let fs = from_wstring(&fs_name);
                        let total_gb = total as f64 / (1024.0 * 1024.0 * 1024.0);
                        let free_gb = free as f64 / (1024.0 * 1024.0 * 1024.0);

                        create_label(hwnd, hinstance, &format!("Drive {}:", drive), x_label, y_pos, 170, 18, false);
                        create_label(hwnd, hinstance, &format!("{:.2} GB total, {:.2} GB free ({})", total_gb, free_gb, fs), x_value, y_pos, 380, 18, false);
                        y_pos += 22;
                    }
                }

                y_pos += 15;

                // Benchmark section
                create_label(hwnd, hinstance, "DISK BENCHMARK", x_label, y_pos, 760, 20, true);
                y_pos += 30;

                create_label(hwnd, hinstance, "Test Size: 50 MB per operation", x_label, y_pos, 760, 18, false);
                y_pos += 25;

                // Benchmark buttons
                for (i, drive) in drives.iter().enumerate().take(4) {
                    let btn_text = format!("Benchmark Drive {}", drive);
                    let btn_id = IDC_BUTTON_BENCH_C + i as i32;
                    create_button(hwnd, hinstance, &btn_text, x_label + (i as i32 * 140), y_pos, 130, 28, btn_id);
                }
            }

            0
        }
        WM_COMMAND => {
            let cmd = (wparam & 0xFFFF) as i32;

            if cmd >= IDC_BUTTON_BENCH_C && cmd <= IDC_BUTTON_BENCH_D + 10 {
                let drives = SystemInfo::get_logical_drives();
                let drive_idx = (cmd - IDC_BUTTON_BENCH_C) as usize;

                if drive_idx < drives.len() {
                    if let Some(ref sysinfo) = G_SYSINFO {
                        let drive = drives[drive_idx];
                        let (read, write) = sysinfo.benchmark_drive(drive);

                        let mut results = sysinfo.benchmark_results.borrow_mut();
                        results.retain(|(d, _, _)| d.chars().next() != Some(drive));
                        results.push((format!("{}", drive), read, write));

                        InvalidateRect(hwnd, null(), 1);
                    }
                }
            }

            0
        }
        WM_PAINT => {
            let mut ps = PAINTSTRUCT {
                hdc: null_mut(),
                fErase: 0,
                rcPaint: RECT { left: 0, top: 0, right: 0, bottom: 0 },
                fRestore: 0,
                fIncUpdate: 0,
                rgbReserved: [0; 32],
            };
            let hdc = BeginPaint(hwnd, &mut ps);

            // Display benchmark results
            if let Some(ref sysinfo) = G_SYSINFO {
                let results = sysinfo.benchmark_results.borrow();
                if !results.is_empty() {
                    let mut y = 450;

                    let title = to_wstring("BENCHMARK RESULTS:");
                    TextOutW(hdc, 20, y, title.as_ptr(), (title.len() - 1) as i32);
                    y += 25;

                    for (drive, read, write) in results.iter() {
                        let result_text = format!("Drive {}: READ {:.2} MB/s  |  WRITE {:.2} MB/s", drive, read, write);
                        let text_w = to_wstring(&result_text);
                        TextOutW(hdc, 20, y, text_w.as_ptr(), (text_w.len() - 1) as i32);
                        y += 22;
                    }
                }
            }

            EndPaint(hwnd, &ps);
            0
        }
        WM_DESTROY => {
            PostQuitMessage(0);
            0
        }
        _ => DefWindowProcW(hwnd, msg, wparam, lparam),
    }
}

unsafe fn create_label(parent: HWND, hinstance: HINSTANCE, text: &str, x: i32, y: i32, w: i32, h: i32, bold: bool) -> HWND {
    let hwnd = CreateWindowExW(
        0,
        to_wstring("STATIC").as_ptr(),
        to_wstring(text).as_ptr(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h,
        parent,
        null_mut(),
        hinstance,
        null_mut(),
    );

    if bold {
        let font = CreateFontW(
            16, 0, 0, 0, FW_BOLD,
            0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FF_DONTCARE,
            to_wstring("MS Sans Serif").as_ptr(),
        );
        let hdc = GetDC(hwnd);
        SelectObject(hdc, font as *mut c_void);
        ReleaseDC(hwnd, hdc);
    }

    hwnd
}

unsafe fn create_button(parent: HWND, hinstance: HINSTANCE, text: &str, x: i32, y: i32, w: i32, h: i32, id: i32) -> HWND {
    CreateWindowExW(
        0,
        to_wstring("BUTTON").as_ptr(),
        to_wstring(text).as_ptr(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        x, y, w, h,
        parent,
        id as HMENU,
        hinstance,
        null_mut(),
    )
}

fn to_wstring(s: &str) -> Vec<u16> {
    use std::os::windows::ffi::OsStrExt;
    std::ffi::OsStr::new(s)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect()
}

fn from_wstring(ws: &[u16]) -> String {
    let len = ws.iter().position(|&c| c == 0).unwrap_or(ws.len());
    String::from_utf16_lossy(&ws[..len])
}

fn main() {
    unsafe {
        let hinstance = GetModuleHandleW(null());

        let class_name = to_wstring("ReactOSBenchmarkClass");
        let wc = WNDCLASSW {
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(wndproc),
            cbClsExtra: 0,
            cbWndExtra: 0,
            hInstance: hinstance,
            hIcon: LoadIconW(null_mut(), IDI_APPLICATION),
            hCursor: LoadCursorW(null_mut(), IDC_ARROW),
            hbrBackground: ((COLOR_BTNFACE + 1) as usize) as HBRUSH,
            lpszMenuName: null(),
            lpszClassName: class_name.as_ptr(),
        };

        if RegisterClassW(&wc as *const WNDCLASSW) == 0 {
            return;
        }

        let title = to_wstring("ReactOS System Information && Disk Benchmark");
        let hwnd = CreateWindowExW(
            0,
            class_name.as_ptr(),
            title.as_ptr(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            820,
            620,
            null_mut(),
            null_mut(),
            hinstance,
            null_mut(),
        );

        if hwnd.is_null() {
            return;
        }

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        let mut msg = MSG {
            hwnd: null_mut(),
            message: 0,
            wParam: 0,
            lParam: 0,
            time: 0,
            pt: POINT { x: 0, y: 0 },
        };

        while GetMessageW(&mut msg as *mut MSG, null_mut(), 0, 0) > 0 {
            TranslateMessage(&msg as *const MSG);
            DispatchMessageW(&msg as *const MSG);
        }
    }
}

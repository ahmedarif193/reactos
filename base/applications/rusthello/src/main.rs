use std::ffi::c_void;
use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};
use std::thread;
use std::time::Duration;

#[repr(C)]
struct FILETIME { dwLowDateTime: u32, dwHighDateTime: u32 }

type BOOL = i32;
type DWORD = u32;
type SIZE_T = usize;

#[link(name = "kernel32")]
extern "system" {
    fn GetLastError() -> DWORD;
    fn Sleep(ms: DWORD);
    fn GetSystemTimeAsFileTime(lp: *mut FILETIME);
    fn GetSystemTimePreciseAsFileTime(lp: *mut FILETIME);
    fn WaitOnAddress(addr: *const c_void, cmp: *const c_void, size: SIZE_T, timeout: DWORD) -> BOOL;
    fn WakeByAddressSingle(addr: *const c_void);
    fn WakeByAddressAll(addr: *const c_void);
}

fn ft_to_u128(ft: &FILETIME) -> u128 {
    ((ft.dwHighDateTime as u128) << 32) | (ft.dwLowDateTime as u128)
}

fn test_gstp() {
    unsafe {
        let mut coarse = FILETIME { dwLowDateTime: 0, dwHighDateTime: 0 };
        let mut precise = FILETIME { dwLowDateTime: 0, dwHighDateTime: 0 };
        GetSystemTimeAsFileTime(&mut coarse);
        GetSystemTimePreciseAsFileTime(&mut precise);
        let c = ft_to_u128(&coarse);
        let p = ft_to_u128(&precise);
        println!("GetSystemTimePreciseAsFileTime: coarse={} precise={} delta={} (100ns)", c, p, p.saturating_sub(c));
        if p >= c { println!("[PASS] GSTPAFT >= GSTAFT"); } else { println!("[FAIL] GSTPAFT < GSTAFT"); }
    }
}

fn test_woa_basic() {
    // Immediate mismatch should return TRUE quickly
    let value: u32 = 1;
    let cmp: u32 = 2;
    let ok = unsafe { WaitOnAddress((&value as *const u32) as *const c_void, (&cmp as *const u32) as *const c_void, std::mem::size_of::<u32>(), 10) };
    println!("WaitOnAddress immediate mismatch returned {}", ok);
    if ok != 0 { println!("[PASS] immediate mismatch"); } else { println!("[FAIL] immediate mismatch"); }

    // Zero-timeout equal should return FALSE + WAIT_TIMEOUT (258)
    let value2: u32 = 5;
    let cmp2: u32 = 5;
    let ok2 = unsafe { WaitOnAddress((&value2 as *const u32) as *const c_void, (&cmp2 as *const u32) as *const c_void, std::mem::size_of::<u32>(), 0) };
    let err2 = unsafe { GetLastError() };
    println!("WaitOnAddress zero-timeout equal returned {}, GetLastError={} (expected 258)", ok2, err2);
    if ok2 == 0 && err2 == 258 { println!("[PASS] zero-timeout equal"); } else { println!("[FAIL] zero-timeout equal"); }
}

fn test_woa_with_wake() {
    // Shared value for wait and wake
    let shared = Arc::new(AtomicU32::new(0));
    let addr = Arc::clone(&shared);

    // Spawn a thread that changes the value after 200ms and wakes all
    let t = thread::spawn(move || {
        thread::sleep(Duration::from_millis(200));
        addr.store(42, Ordering::SeqCst);
        unsafe { WakeByAddressAll((&*addr as *const AtomicU32) as *const c_void); }
    });

    // Main waits until value != 0 or timeout
    let cmp: u32 = 0;
    let ok = unsafe {
        WaitOnAddress(((&*shared) as *const AtomicU32) as *const c_void, (&cmp as *const u32) as *const c_void, std::mem::size_of::<u32>(), 5000)
    };
    let final_v = shared.load(Ordering::SeqCst);
    println!("WaitOnAddress wake: ret={} value={}", ok, final_v);
    if ok != 0 && final_v == 42 { println!("[PASS] wait/wake by address"); } else { println!("[FAIL] wait/wake by address"); }

    let _ = t.join();

    // Two waiters, Single then All
    shared.store(100, Ordering::SeqCst);
    let a1 = Arc::clone(&shared);
    let a2 = Arc::clone(&shared);
    let waiter = |id: &'static str, a: Arc<AtomicU32>| -> thread::JoinHandle<bool> {
        thread::spawn(move || {
            let cmpv: u32 = 100;
            let ok = unsafe { WaitOnAddress(((&*a) as *const AtomicU32) as *const c_void, (&cmpv as *const u32) as *const c_void, std::mem::size_of::<u32>(), 2000) };
            println!("waiter {} woke: {} value={}", id, ok, a.load(Ordering::SeqCst));
            ok != 0
        })
    };

    let w1 = waiter("A", a1);
    let w2 = waiter("B", a2);
    unsafe { Sleep(100); }
    shared.store(101, Ordering::SeqCst);
    unsafe { WakeByAddressSingle(((&*shared) as *const AtomicU32) as *const c_void); }
    unsafe { Sleep(100); }
    unsafe { WakeByAddressAll(((&*shared) as *const AtomicU32) as *const c_void); }
    let r1 = w1.join().unwrap_or(false);
    let r2 = w2.join().unwrap_or(false);
    println!("WaitByAddressSingle/All results: A={} B={}", r1, r2);
    if r1 || r2 { println!("[INFO] at least one waiter woke on Single"); }
    if r1 && r2 { println!("[PASS] both waiters woke eventually"); } else { println!("[WARN] not all waiters observed wake"); }
}

fn main() {
    println!("RustHello: testing Win8+ sync/time APIs\n");
    test_gstp();
    test_woa_basic();
    test_woa_with_wake();
}

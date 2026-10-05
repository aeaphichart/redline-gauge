
---

## Which file? / ใช้ไฟล์ไหน

| File / ไฟล์ | Use when / ใช้เมื่อ |
|---|---|
| `…-invert-factory.bin` | **Start here.** Most CYD boards. ส่วนใหญ่ใช้ตัวนี้ |
| `…-noinvert-factory.bin` | Colours look inverted (white background) with the file above. จอสีกลับด้าน พื้นขาว |
| `…-invert-dim-factory.bin` | Same as *invert*, with BRIGHTNESS control, for boards whose backlight dims (if the screen goes black at 50 %, use *invert*). บอร์ดที่หรี่ไฟจอได้ |
| `…-noinvert-dim-factory.bin` | Same as *noinvert*, with BRIGHTNESS control. จอสีปกติ + หรี่ไฟจอได้ |
| `…-update.bin` | Upgrading a board that already runs REDLINE: keeps settings and the run log. อัปเดตโดยไม่ล้างค่าที่ตั้งไว้ |

## How to flash / วิธีแฟลช (no compiling needed / ไม่ต้องคอมไพล์)

**REDLINE Flasher (easiest):** download `REDLINE-Flasher-windows.exe` from the
[latest release](https://github.com/moomdate/redline-gauge/releases/latest) (macOS / Linux builds are there too)
and run it.
1. Plug in the board. The flasher picks the CYD's port (CH340) by itself; **Refresh** re-scans.
2. Choose the version (it downloads it from GitHub), the panel type, and **update** (keeps settings) or
   **factory** (first install). **Local file** flashes a `.bin` you already have.
3. Click **Flash**. The board restarts by itself.

ดาวน์โหลด `REDLINE-Flasher-windows.exe` จากหน้า release ล่าสุดแล้วเปิดได้เลย
- โปรแกรมเลือกพอร์ตของบอร์ด CYD ให้เอง และเปลี่ยนเองได้
- เลือกเวอร์ชัน (โปรแกรมโหลดไฟล์ให้), แบบจอ และ update (เก็บค่าที่ตั้งไว้) หรือ factory (ลงใหม่)
- กด **Flash** ก็เสร็จ

Windows may show "Windows protected your PC" because the app isn't code-signed: click **More info → Run anyway**.
No COM port? Install the **CH340 driver** (WCH "CH341SER").
Windows อาจขึ้นเตือนเพราะโปรแกรมไม่มีลายเซ็น ให้กด More info → Run anyway · ไม่เห็นพอร์ต: ลงไดรเวอร์ CH340
macOS: `chmod +x REDLINE-Flasher-macos` then right-click → Open the first time (unsigned app).

The flasher also works in a terminal:
`REDLINE-Flasher --cli` (prompts), or `--port COM5 --variant invert --mode update --version latest -y`.

**Without installing anything, in the browser (Chrome / Edge):**
1. Open <https://espressif.github.io/esptool-js/> and plug in the board.
2. Click **Connect** and pick the board's port (CH340 / USB serial).
3. Add the file:
   - `*-factory.bin` at address **0x0**, or
   - `*-update.bin` at address **0x10000**.
4. Click **Program**, then press the board's RST button (or unplug and replug it).

**Command line:**
```
pip install "esptool<5"
esptool.py --chip esp32 write_flash 0x0      redline-vX.Y.Z-invert-factory.bin   # first install
esptool.py --chip esp32 write_flash 0x10000  redline-vX.Y.Z-invert-update.bin    # upgrade, keeps settings
```

The factory image also erases the saved settings and run log. `SHA256SUMS-*.txt` has the checksums.

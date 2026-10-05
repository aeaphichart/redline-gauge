
---

## Which file? / ใช้ไฟล์ไหน

| File / ไฟล์ | Use when / ใช้เมื่อ |
|---|---|
| `…-invert-factory.bin` | **Start here.** Most CYD boards. ส่วนใหญ่ใช้ตัวนี้ |
| `…-noinvert-factory.bin` | Colours look inverted (white background) with the file above. จอสีกลับด้าน พื้นขาว |
| `…-invert-dim-factory.bin` | Same as *invert*, with BRIGHTNESS control, for boards whose backlight dims (if the screen goes black at 50 %, use *invert*). บอร์ดที่หรี่ไฟจอได้ |
| `…-update.bin` | Upgrading a board that already runs REDLINE: keeps settings and the run log. อัปเดตโดยไม่ล้างค่าที่ตั้งไว้ |

## How to flash / วิธีแฟลช (no compiling needed / ไม่ต้องคอมไพล์)

**In the browser (Chrome / Edge):**
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

# Tối ưu tự động (`s40-tune`)

Hồ sơ nằm ở `payload/opt/s40/tune/profile.json` (trên máy: `/opt/s40/tune/profile.json`) và được áp dụng bởi `s40-tune.sh` mỗi lần bật máy.
Lệnh: `sudo s40-tune.sh apply | status | revert | reset-retroarch`. Tệp gốc được sao lưu một lần ở `~/.config/s40/tune/backup`.

## RetroArch 64 bit và 32 bit (`retroarch.cfg`)

| Khóa | Giá trị | Lý do |
|---|---|---|
| `savestate_auto_load` | `false` | khi chưa có file lưu, RetroArch báo lỗi và game có thể đơ một nhịp; không ép tải |
| `savestate_auto_save` | `false` | không ghi trạng thái ra thẻ nhớ mỗi lần thoát (đỡ chờ và đỡ mòn thẻ) |
| `network_cmd_enable` | `false` | tắt cổng điều khiển mạng, đỡ tốn CPU và mở cổng không cần thiết |

## Tùy chọn lõi (`retroarch-core-options.cfg`, cả hai bản)

| Khóa | Giá trị | Lý do |
|---|---|---|
| `fbneo-frameskip-v2` | `auto` | arcade nặng giữ đủ tốc độ |
| `fbneo-fm-interpolation`, `fbneo-sample-interpolation` | `linear` | nội suy 4 điểm tốn CPU, tuyến tính đủ tốt và nhẹ hơn |
| `gpsp_frameskip`, `mgba_frameskip`, `snes9x_frameskip` | `auto` | bỏ khung khi cần để giữ tốc độ |
| `pcsx_rearmed_drc` | `enabled` | dynarec cho PS1 |
| `pcsx_rearmed_frameskip_type` | `auto` | giữ tốc độ PS1 |
| `pcsx_rearmed_neon_enhancement_enable`, `..._no_main` | `disabled` | tăng cường NEON làm chậm trên RK3326 |
| `pcsx_rearmed_spu_interpolation` | `simple` | âm thanh PS1 nhẹ hơn |

## PPSSPP

| Tệp | Thay đổi | Lý do |
|---|---|---|
| `controls.ini` | `Pause = 1-111,10-110`; `Save State`, `Load State` để trống | nút FN mở menu; nhấn cần analog (L3/R3) không còn gây Lưu/Tải trạng thái |
| `/opt/ppsspp/assets/gamecontrollerdb.txt` | gamepad "GO-Super": `rightstick:b15`, `guide:b16` | FN (nút 16) thành nút menu; R3 là nút bấm thường |

## Thư viện hệ thống

dArkOS dùng bản SDL2 riêng và `libmali` thay cho thư viện Debian. Nếu một gói apt trỏ lại liên kết `libSDL2` hoặc `libwayland-egl` sang bản Debian/libMali, các ứng dụng SDL, mpv và trình duyệt ngừng chạy. `s40-tune.sh apply` kiểm tra và sửa lại hai liên kết đó.

## Vì sao bền qua cập nhật

`s40-tune.service` (oneshot, bật khi khởi động) chạy `s40-tune.sh apply` ở mỗi lần bật máy. Nếu bản cập nhật dArkOS ghi đè một trong các tệp trên, lần bật sau hồ sơ được áp dụng lại. Lệnh chạy lại được nhiều lần và không đụng vào khóa nào ngoài hồ sơ.

## Giới hạn

- Hồ sơ chỉ gồm các thay đổi đã kiểm chứng trên máy thật; DraStic, mupen64plus và hiệu năng CPU giữ nguyên của dArkOS.
- Nếu một bản cập nhật xóa luôn `/opt/s40` hoặc dịch vụ, cần chạy lại `install.sh`.

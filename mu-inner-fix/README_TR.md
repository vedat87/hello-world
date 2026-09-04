# MU Main 5.2 / V43 — Inner 135/136 NoBitmapLight V2

Bu paket, BK **Greater Life / Inner master skill** kullanıldığında yeni `Main.exe` istemcisinin donup kapanmasını izole etmek için hazırlanmıştır.

## Ne değişir?

Yalnız `WSclient.cpp` içindeki üç görsel yol değiştirilir:

1. `InsertBuffPhysicalEffect` içinde `EFFECT_GREATER_LIFE_ENHANCED (135)` ve `EFFECT_GREATER_LIFE_MASTERED (136)` için ikinci `BITMAP_LIGHT` oluşturulmaz.
2. `ClearBuffPhysicalEffect` içinde 135/136 için oluşturulmayan fiziksel ışık tekrar silinmeye çalışılmaz.
3. Inner receive/cast animation yolunda master `360/363` için ikinci `DeleteEffect/CreateEffect(BITMAP_LIGHT)` döngüsü atlanır. Eski/normal Greater Life yolu korunur.

**Dokunulmayanlar:** skill 356/360/363, Effect 135/136, server HP hesabı, logical buff kaydı, master tree ve network buff state.

## Uygulama

PowerShell açıp kaynak klasöründe çalıştırın:

```powershell
powershell -ExecutionPolicy Bypass -File .\APPLY_INNER_135_136_NOBITMAPLIGHT_V2.ps1 -SourceRoot "C:\MainSource"
```

Script doğru `WSclient.cpp` dosyasını tek başına bulursa patch uygular. Birden fazla kaynak kopyası varsa doğrudan dosya yolunu verin:

```powershell
powershell -ExecutionPolicy Bypass -File .\APPLY_INNER_135_136_NOBITMAPLIGHT_V2.ps1 -File "C:\MainSource\source\WSclient.cpp"
```

Patch uygulanmadan önce güvenlik kontrolleri yapılır. Beklenen üç kod kalıbından biri bulunmazsa **dosya değiştirilmez**.

Otomatik yedek:

`WSclient.cpp.inner135136_v2.bak`

Geri alma:

```powershell
powershell -ExecutionPolicy Bypass -File .\APPLY_INNER_135_136_NOBITMAPLIGHT_V2.ps1 -File "C:\MainSource\source\WSclient.cpp" -Restore
```

## Derleme

- Visual Studio 2026
- Release
- Win32 / x86
- MSVC v145

Yeni `Main.exe` ile aynı BK karakterinde master Inner 360/363 aktifken test edin.

## Test sonucu nasıl yorumlanır?

- Inner artık kapanmıyorsa: crash 135/136 fiziksel `BITMAP_LIGHT` yaşam döngüsündedir; sonraki adım bypass yerine temiz kalıcı efekt implementasyonudur.
- Yine kapanıyorsa: bu görsel yol elenir ve bir sonraki hedef native `0x2D` buff packet / struct packing / MaxLife update yoludur.

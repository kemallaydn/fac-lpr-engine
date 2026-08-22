# FAC LPR .NET P/Invoke sample

`FacLpr.Sample` public C ABI v1'i doğrudan P/Invoke ile tüketen .NET 8 console consumer örneğidir.

Örnek şu contract'ları gösterir:

- `cdecl` native symbol declarations;
- version query ve ABI guard;
- opaque engine handle create/destroy lifecycle;
- caller-owned BGR image buffer pinning;
- two-call result-buffer sizing/ownership;
- native status + `fac_lpr_get_last_error_v1` -> managed exception mapping;
- repeated create/destroy working-set smoke guard.

Windows x64 release DLL'i build edildikten sonra `fac_lpr_engine.dll` ve runtime bağımlılıklarının bulunduğu dizini `PATH` içine ekleyip çalıştırın:

```powershell
$env:PATH = "$PWD\build\package-windows\artifacts\bin\Release;$env:PATH"
dotnet run --project samples/dotnet/FacLpr.Sample/FacLpr.Sample.csproj -c Release -- --iterations 10000
```

`RecognizeBgr` JPEG/PNG decode etmez; consumer'ın sahip olduğu packed BGR8 frame buffer'ını pinleyip native C ABI'ya geçirir. Dönen byte dizisi v1 flat result buffer'dır ve nested veriler buffer başlangıcına göre offset/length ile çözülmelidir.

Working-set kontrolü yalnız catastrophic interop handle leak smoke guard'ıdır. Native long-run leak doğrulamasının yerine geçmez; ana memory/sanitizer harness'leri ayrıca çalıştırılır.

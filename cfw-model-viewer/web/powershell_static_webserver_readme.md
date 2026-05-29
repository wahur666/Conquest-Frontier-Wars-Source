# PowerShell Static Web Server

## server.ps1

```powershell
$root = (Get-Location).Path

$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add("http://localhost:8080/")
$listener.Start()

Write-Host "Serving $root at http://localhost:8080/"

$mimeTypes = @{
    ".html" = "text/html"
    ".css"  = "text/css"
    ".js"   = "application/javascript"
    ".json" = "application/json"
    ".png"  = "image/png"
    ".jpg"  = "image/jpeg"
    ".jpeg" = "image/jpeg"
    ".gif"  = "image/gif"
    ".svg"  = "image/svg+xml"
    ".txt"  = "text/plain"
    ".ico"  = "image/x-icon"
}

while ($listener.IsListening) {
    try {
        $context  = $listener.GetContext()
        $request  = $context.Request
        $response = $context.Response

        $urlPath = [Uri]::UnescapeDataString($request.Url.AbsolutePath.TrimStart('/'))

        if ([string]::IsNullOrWhiteSpace($urlPath)) {
            $urlPath = "index.html"
        }

        $filePath = Join-Path $root $urlPath

        if ((Test-Path $filePath) -and !(Get-Item $filePath).PSIsContainer) {

            $ext = [System.IO.Path]::GetExtension($filePath).ToLower()

            if ($mimeTypes.ContainsKey($ext)) {
                $response.ContentType = $mimeTypes[$ext]
            }
            else {
                $response.ContentType = "application/octet-stream"
            }

            $bytes = [System.IO.File]::ReadAllBytes($filePath)

            $response.ContentLength64 = $bytes.Length
            $response.OutputStream.Write($bytes, 0, $bytes.Length)
        }
        else {
            $response.StatusCode = 404

            $msg = "404 - File not found"
            $bytes = [Text.Encoding]::UTF8.GetBytes($msg)

            $response.ContentType = "text/plain"
            $response.ContentLength64 = $bytes.Length
            $response.OutputStream.Write($bytes, 0, $bytes.Length)
        }

        $response.OutputStream.Close()
    }
    catch {
        Write-Host $_
    }
}
```

---

# Setup

## Allow local PowerShell scripts to run

Open PowerShell and run:

```powershell
Set-ExecutionPolicy RemoteSigned -Scope CurrentUser
```

Confirm:

```powershell
Get-ExecutionPolicy -List
```

You should see:

```text
CurrentUser    RemoteSigned
```

---

# Running the server

Save the script as:

```text
server.ps1
```

Run it from the folder you want to serve:

```powershell
.\server.ps1
```

Open:

```text
http://localhost:8080/
```

Examples:

```text
http://localhost:8080/
http://localhost:8080/index.html
http://localhost:8080/css/site.css
http://localhost:8080/js/app.js
http://localhost:8080/images/logo.png
```

---

# Notes

`RemoteSigned` allows local scripts to run, but downloaded scripts may need to be unblocked.

```powershell
Unblock-File .\server.ps1
```

To revert the policy:

```powershell
Set-ExecutionPolicy Restricted -Scope CurrentUser
```

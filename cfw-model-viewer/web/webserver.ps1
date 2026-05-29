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
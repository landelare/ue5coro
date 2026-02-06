# HTTP

The `UE5Coro::Http` namespace currently houses one single function to perform
HTTP requests asynchronously.

Most of the HTTP API remains on the engine's IHttpRequest.

### auto ProcessAsync(FHttpRequestRef Request)

This function calls ProcessRequest() on its parameter, and returns an object
that can be awaited to resume the coroutine when the request has finished.

The await expression will result in a TTuple of the FHttpResponsePtr and
bConnectedSuccessfully.

Example:
```cpp
using namespace UE5Coro::Http;

FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
Request->SetURL(TEXT("https://www.example.com"));
if (auto [Response, bConnectedSuccessfully] = co_await ProcessAsync(Request);
    Response && bConnectedSuccessfully)
{
    FString Content = Response->GetContentAsString();
    // ...
}
```

> [!WARNING]
> Unreal Engine 5.3 introduced EHttpRequestDelegateThreadPolicy.
> This is fully supported, however, it initially shipped broken in the engine.
>
> Using EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread can result in the
> request being stuck forever, which will result in the coroutine not resuming.

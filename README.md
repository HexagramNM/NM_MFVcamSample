# NM_MFVCamSample

![NM_MFVCamSample_Result](ReadmeAssets/NM_MFVCamSample_Result.gif)

Window11から使用できるようになったMediaFoundationによる仮想カメラのサンプルです。
WinRTのWindowsGraphicsCaptureAPIでキャプチャしたウィンドウを映せるようにしています。

仮想カメラ用のdllを作成するためのソースコードはSimon Mourier氏の
[VCamSample](https://github.com/smourier/VCamSample)のコードを
使わせていただいております。


## 動作環境

* OS: **Windows11**（MediaFoundationの仮想カメラの機能は
Windows11 (Windows Build 22000以降)から追加されたものであるため、
Windows10以前だと動きません。）

* CPU: 13th Gen Intel(R) Core(TM) i7-13700H 2.40 GHz

* RAM: 32.0GB

* GPU: NVIDIA GeForce RTX 4060 Laptop GPU (**DirectX11使用**)

太字の要件を満たしているマシンであれば、動作すると思います。


## 仮想カメラを使用できたソフトウェア

* Windows標準のカメラアプリ

* Discord

* Zoom

* Teams

* [ウェブカメラテスト](https://ja.webcamtests.com/)


## やっていること

Media Foundationの仮想カメラはFrameServerというサービス上で、Local Serviceのアカウントで
動作します。セッション0であるため、ユーザアカウントに表示しているウィンドウのハンドルを
取得することができません。

そこで、ユーザ側のアプリケーションで、キャプチャしたウィンドウの画面を共有テクスチャとして
保存し、FrameServer上の仮想カメラでこの共有テクスチャを参照できるようにしております。
ウィンドウのサイズなどのパラメタもこの方法で転送しております。

共有テクスチャの作成には
[IDXGIResource1::CreateSharedHandle](https://learn.microsoft.com/ja-jp/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiresource1-createsharedhandle)
を使用しております。


## プロジェクト構成

* `NM_CaptureWindow` (C++)... ウィンドウハンドルからウィンドウをキャプチャし、
仮想カメラでアクセスできる共有テクスチャの作成を行うdllです。
`SampleDriverApp`でこのdllが使われます。

仮想カメラの起動も`NM_CaptureWindow`で行っております。

* `VCamSampleSource` (C++)... Media Foundationによる仮想カメラの本体です。
基本的には[VCamSample](https://github.com/smourier/VCamSample)をそのまま使用しておりますが、
以下の部分を変更しております。

    - `FrameGenerator.h/.cpp`... 共有テクスチャを映し出せるように改造しています。

    - `MediaStream.cpp`... 仮想カメラの解像度を変更するために`NUM_IMAGE_COLS`,
    `NUM_IMAGE_ROWS`の値を変更しております。

* `SampleDriverApp` (C#)... WPFで作ったサンプルのGUIです。
プログレスバーを適当に動かしております。
`NM_CaptureWindow.cs`で`NM_CaptureWindow.dll`にアクセスできるようにしております。


## ビルド方法

1. `NM_MFVCamSample.sln`を開き、ソリューションをビルドします。

    - VisualStudio2022でプロジェクトを作っています。

    - `NM_CaptureWindow`はビルド後、生成された`NM_CaptureWindow.dll`が
    `SampleDriverApp.exe`のあるフォルダにコピーされるように設定しております。

2. `NM_MFVCamSample`のフォルダに`Users`の読み取りと実行のアクセス権限を付与する。
フォルダのプロパティから設定できます。

    ![SecuritySetting](ReadmeAssets/SecuritySetting.png)

    - FrameServerが仮想カメラのdllである`x64/[Release or Debug]/VCamSampleSource.dll`
    にアクセスできるようにするため

3. `RegisterVirtualCamera.bat`の中身を確認した上で、管理者権限で実行し、
ビルドされた仮想カメラをインストールする。

    - Releaseビルドしたものを登録するようにしているので、Debugビルドのものを登録する場合は
    Releaseの部分をDebugに書き換える。

    - アンインストールする場合は、`UnregisterVirtualCamera.bat`の中身を確認した上で、
    管理者権限で実行する。
    こちらもReleaseビルド用になっているので、必要に応じてDebugに書き換える。

4. `SampleDriverApp/bin/[Release or Debug]/net7.0-windows/SampleDriverApp.exe`を起動する。

    - 起動するとカメラを使用するアプリケーションのカメラ一覧に
    `NM_Capture_Window_VCam_Sample`が出現する。
    これを選択すると`SampleDriverApp`の画面がカメラ上に表示される。

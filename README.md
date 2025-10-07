<h1 align="center">
  <img height="120px" src="https://upload.wikimedia.org/wikipedia/commons/d/d9/Node.js_logo.svg" />&nbsp;&nbsp;&nbsp;&nbsp;
  <img height="120px" src="https://webrtc.github.io/webrtc-org/assets/images/webrtc-logo-vert-retro-dist.svg" />
</h1>

<h2>Update v0.4.8</h2>

Added native recording stream. Use new methods recording from peerConnection as peerConnection.attachAudioSink and event encodedframe from RTCVideoSink.

Example:

```javascript
recordVideoNative(videoTrack, audioTrack) {
        const { RTCVideoSink } = this.webrtc.nonstandard;
        const payloadTypes = this.peerConnection.getPayloadTypes();
        console.log("Automatically detected Payload Types:", payloadTypes);
        if (payloadTypes.h264) {
            this.videoCodec = 'h264';
        } else if (payloadTypes.vp8) {
            this.videoCodec = 'vp8';
        } else {
            console.error("No supported video codec (H.264 or VP8) found in SDP.");
            return;
        }
        console.log(`Using video codec: ${this.videoCodec}`);
        fs.mkdirSync(this.streamDir, { recursive: true });
        const previewLink = path.join(this.streamDir, 'video_preview.ivf');
        this.videoRawStream = null;
        this.audioRawStream = fs.createWriteStream(path.join(this.streamDir, 'audio.opus'));
        this.videoPreviewStream = fs.createWriteStream(previewLink);

        console.log('Current media data:', this.peerConnection.getActiveCodecs());

        (async () => {
            if (audioTrack) {
                try {
                    console.log('Attaching Frame sink for AUDIO');
                    this.oggWriter = new OggOpusWriter(this.audioRawStream);
                    
                    let limitLogs = 10;
                    this.peerConnection.attachAudioSink(audioTrack.id, (event) => {
                        const { frame, rtpTimestamp } = event;
                        if(rtpTimestamp === null || rtpTimestamp === undefined) {
                            return;
                        }
                        if(limitLogs<10){
                            console.log(`Writing Opus packet, size: ${frame.length}, rtpTimestamp: ${rtpTimestamp}`);
                            limitLogs++;
                        }
                        if(this._isPause) {
                            this.lastRealAudioTimestamp ??= rtpTimestamp;
                            this.pause(false);
                        }
                        this.oggWriter.addPacket(frame, rtpTimestamp);
                        this.lastRealAudioTimestamp = rtpTimestamp;
                        
                    });
                    console.log("Audio sink started successfully.");
                } catch (e) {
                    console.log(e);
                }
            }
        })();

        (async () => {
            if (videoTrack) {
                this.RTCVideoSink = new RTCVideoSink(videoTrack);
                let ivfHeaderWritten = false;


                try {
                    console.log('Attaching Frame sink for VIDEO');
                    const limitPreview = 24;
                    let iterationPreview = 0;
                    this.RTCVideoSink.addEventListener('encodedframe', (event) => {
                        const { frame, isKeyFrame, resolution, timestampUs } = event.data;

                        
                        if (!ivfHeaderWritten) {
                            if (!isKeyFrame) {
                                console.log('Skipping initial non-key-frames...');
                                return;
                            }

                            console.log(`Starting record with key frame. Resolution: ${resolution.width}x${resolution.height}`);
                            this.videoRawStream = fs.createWriteStream(path.join(this.streamDir, `video.ivf`))

                            this._writeVideoHeaders(this.videoRawStream, resolution);
                            this._writeVideoHeaders(this.videoPreviewStream, resolution);
                            ivfHeaderWritten = true;
                        }
                        if (!this.videoRawStream) console.log("resolution:", resolution);
                        
                        if (!this.videoRawStream) return;

                        this._writeVideoFrameHeader(this.videoRawStream, frame, timestampUs);

                        this.videoRawStream.write(frame);
                        if(iterationPreview < limitPreview){
                            this._writeVideoFrameHeader(this.videoPreviewStream, frame, timestampUs);
                            try{
                                if(!this.videoPreviewStream.closed) this.videoPreviewStream.write(frame);
                            }catch (e){}
                            iterationPreview++;
                            if(iterationPreview===limitPreview-1){
                                try{
                                    this.videoPreviewStream.end();
                                }catch (e){}
                                this._generateVideoPreview(previewLink);
                            }
                        }
                    });

                    console.log("Video sink started successfully.");
                } catch (e) {
                    console.log(e);
                }
            }
        })();
    }
```

[![NPM](https://img.shields.io/npm/v/wrtc.svg)](https://www.npmjs.com/package/wrtc) [![macOS/Linux Build Status](https://circleci.com/gh/node-webrtc/node-webrtc/tree/develop.svg?style=shield)](https://circleci.com/gh/node-webrtc/node-webrtc) [![Windows Build status](https://ci.appveyor.com/api/projects/status/iulc84we28o1i7b9?svg=true)](https://ci.appveyor.com/project/markandrus/node-webrtc-7bnua)

node-webrtc is a Node.js Native Addon that provides bindings to [WebRTC M81](https://chromium.googlesource.com/external/webrtc/+/branch-heads/4044). This project aims for spec-compliance and is tested using the W3C's [web-platform-tests](https://github.com/web-platform-tests/wpt) project. A number of [nonstandard APIs](docs/nonstandard-apis.md) for testing are also included.

Install
-------

```
npm install wrtc
```

Installing from NPM downloads a prebuilt binary for your operating system × architecture. Set the `TARGET_ARCH` environment variable to "arm" or "arm64" to download for armv7l or arm64, respectively. Linux and macOS users can also set the `DEBUG` environment variable to download debug builds.

You can also [build from source](docs/build-from-source.md).

Supported Platforms
-------------------

The following platforms are confirmed to work with node-webrtc and have prebuilt binaries available. Since node-webrtc targets [N-API version 3](https://nodejs.org/api/n-api.html), there may be additional platforms supported that are not listed here. If your platform is not supported, you may still be able to [build from source](docs/build-from-source.md).

<table>
  <thead>
    <tr>
      <td colspan="2" rowspan="2"></td>
      <th colspan="3">Linux</th>
      <th>macOS</th>
      <th>Windows</th>
    </tr>
    <tr>
      <th>armv7l</th>
      <th>arm64</th>
      <th>x64</th>
      <th>x64</th>
      <th>x64</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th rowspan="6">Node</th>
      <th>8</th>
        <td align="center">✓</td>
        <td align="center">✓</td>
        <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
    <tr>
      <th>10</th>
        <td align="center">✓</td>
        <td align="center">✓</td>
        <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
    <tr>
      <th>11</th>
        <td align="center">✓</td>
        <td align="center">✓</td>
        <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
    <tr>
      <th>12</th>
        <td align="center">✓</td>
        <td align="center">✓</td>
        <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
    <tr>
      <th>13</th>
        <td align="center">✓</td>
        <td align="center">✓</td>
        <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
    <tr>
      <th>14</th>
        <td align="center">✓</td>
        <td align="center">✓</td>
        <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
    <tr>
      <th rowspan="2">Electron</th>
      <th>4</th>
        <td align="center"></td>
        <td align="center"></td>
        <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
    <tr>
      <th>5</th>
        <td align="center"></td>
        <td align="center"></td>
        <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
  </tbody>
</table>

Examples
--------

See [node-webrtc/node-webrtc-examples](https://github.com/node-webrtc/node-webrtc-examples).

Contributing
------------

Contributions welcome! Please refer to the [wiki](https://github.com/node-webrtc/node-webrtc/wiki/Contributing).

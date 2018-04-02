# Companion scripts for ESParkle

## `esparkle_mp3.php`
````
/**
 * MP3 handler
 *
 * This is a companion script for ESParkle
 * See <https://github.com/CosmicMac/ESParkle>
 *
 * USE
 *  - http//<host>/<path>/esparkle_mp3.php?action=random
 *    Play random MP3 from default random dir
 *
 *  - http//<host>/<path>/esparkle_mp3.php?action=random&dir=<dir>
 *    Play random MP3 from <dir>
 *
 *  - http//<host>/<path>/esparkle_mp3.php?action=list
 *    Get the list of all available MP3s as a JSON string
 *
 * CHANGES
 *  - 20180329 V1.0 Initial version
 */
````

## `esparkle_tts.php`
````
/**
 * Proxy to AWS Polly TTS service
 *
 * See <https://docs.aws.amazon.com/polly/latest/dg/what-is.html>
 *
 * This is a companion script for ESParkle
 * See <https://github.com/CosmicMac/ESParkle>
 *
 * USE
 *  - http//<host>/<path>/esparkle_tts.php?voice=<voice>&text=<text>
 *    Send TTS request to Polly and get URL of generated MP3 file
 *    in return.
 *
 *    <voice> can be omitted (AWS_VOICE constant is used by default).
 *    See <https://docs.aws.amazon.com/polly/latest/dg/voicelist.html>
 *
 *    <text> can contain SSML markup.
 *    See <https://docs.aws.amazon.com/polly/latest/dg/ssml.html>
 *
 *    Parameters may be POSTed as well.
 *
 * CHANGES
 *  - 20180329 V1.0 Initial version
 */
````

<?php
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

//############################################################################
// SETTINGS
//############################################################################

define('DEBUG', false);

// SECURITY /!\
// You should consider security seriously since malicious use
// of this script may have a huge impact on your AWS bill
define('HTTP_AUTH_USER', 'YOUR_HTTP_AUTH_USER');                              // HTTP Basic authentication user name
define('HTTP_AUTH_PASSWORD', 'YOUR_HTTP_AUTH_PASSWORD');                      // HTTP Basic authentication password
define('AUTHORIZED_IPS', 'YOUR_IP_1,YOUR_IP_2');                              // List of authorized IPs, comma separated (leave empty for any IP)

// AWS
define('AWS_ACCESS_KEY_ID', 'YOUR_AWS_ACCESS_KEY_ID');                        // Your AWS access key
define('AWS_SECRET_KEY', 'YOUR_AWS_SECRET_KEY');                              // Your AWS secret
define('AWS_REGION', 'us-west-1');                                            // See https://docs.aws.amazon.com/en_en/general/latest/gr/rande.html#pol_region
define('AWS_VOICE', 'Matthew');                                               // Default voice, see https://docs.aws.amazon.com/polly/latest/dg/voicelist.html

//############################################################################

// Display error messages (should be disabled in production)
if (DEBUG) {
    ini_set('display_errors', 1);
    ini_set('display_startup_errors', 1);
    error_reporting(E_ALL);;
}

// Define MP3 directory from current script directory
define('MP3_DIR', __DIR__ . '/mp3/tts/');

// Define MP3 base URL from current script URL
define('MP3_BASE_URL', (isset($_SERVER['HTTPS']) ? 'https' : 'http')
    . "://$_SERVER[SERVER_NAME]:$_SERVER[SERVER_PORT]"
    . substr($_SERVER['SCRIPT_NAME'], 0, strrpos($_SERVER['SCRIPT_NAME'], '/'))
    . '/mp3/tts/');

// Set output as plain text
header('Content-Type: text/plain');

// Limit access to known IPs
if (AUTHORIZED_IPS && !in_array($_SERVER['REMOTE_ADDR'], explode(',', AUTHORIZED_IPS))) {
    header('HTTP/1.0 403 Forbidden');
    //http_response_code(403);
    die('Access denied');
}

// Authenticate user
if (preg_match('/^Basic\s+(.+)$/', @$_SERVER['HTTP_AUTHORIZATION'], $match)) {
    /*
     * FastCGI server doesn't expose PHP_* variable
     * As a workaround, just create a .htaccess file with this directive:
     * SetEnvIf Authorization "(.*)" HTTP_AUTHORIZATION=$1
     */
    list($_SERVER['PHP_AUTH_USER'], $_SERVER['PHP_AUTH_PW']) = explode(':', base64_decode($match[1]));
}
if (@$_SERVER['PHP_AUTH_USER'] != HTTP_AUTH_USER || $_SERVER['PHP_AUTH_PW'] != HTTP_AUTH_PASSWORD) {
    header('WWW-Authenticate: Basic realm="ESParkle TTS"');
    http_response_code(401);
    die('Authentication required');
}

// Get mandatory text param
$text = @$_REQUEST['text'];
if (!$text) {
    http_response_code(400);
    die('Text is missing');
}

// Get optional voice param
$voice = @$_REQUEST['voice'] ?: AWS_VOICE;

// Eventually create mp3 directory
if (!is_dir(MP3_DIR)) {
    mkdir(MP3_DIR, 0777, true);
}

// Set mp3 file name
$filename = md5(strtoupper($voice . $text)) . '.mp3';

// Create mp3 file if it does not exist
if (!file_exists(MP3_DIR . $filename)) {

    try {
        require_once 'aws/aws-autoloader.php';

        $polly = new \Aws\Polly\PollyClient([
            'version'     => '2016-06-10',
            'credentials' => new \Aws\Credentials\Credentials(AWS_ACCESS_KEY_ID, AWS_SECRET_KEY),
            'region'      => AWS_REGION
        ]);

        $speech = $polly->synthesizeSpeech([
            'OutputFormat' => 'mp3',
            'SampleRate'   => '16000',  // 8000, 16000, 22050
            'Text'         => $text,
            'TextType'     => strpos($text, '<speak>') === 0 ? 'ssml' : 'text',
            'VoiceId'      => $voice
        ]);

        $mp3 = $speech->get('AudioStream')->getContents();

        // Save MP3 file and append file name to catalog, with associated voice and text
        if (file_put_contents(MP3_DIR . $filename, $mp3)) {
            file_put_contents(MP3_DIR . 'catalog.csv', date('c') . "\t$filename\t$voice\t$text\n", FILE_APPEND | LOCK_EX);
        }

    } catch (Exception $e) {
        // Something went wrong...
        die($e->getMessage());
    }
}

// Send mp3 file URL in HTTP response body
die(MP3_BASE_URL . $filename);

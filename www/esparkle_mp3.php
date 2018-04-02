<?php
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

//############################################################################
// SETTINGS
//############################################################################

// Paths, relative to current script directory
define('BASE_DIR', 'mp3');                                                    // Base MP3 directory
define('DEFAULT_RANDOM_DIR', 'mp3/famous_movies_dialogs');                    // Default random MP3 directory

//############################################################################

switch (@$_GET['action']) {
    case 'random':
        $dir = preg_replace('/^\/+/', '', str_replace('.', '', @$_GET['dir'])) ?: DEFAULT_RANDOM_DIR;
        getRandomMp3($dir);
        break;
    case 'list':
        die(getMP3List());
        break;
    default:
        http_response_code(400);
        die('Invalid action');
}
exit;

/**
 * Get random MP3 file from directory
 *
 * @param string $dirname
 */
function getRandomMp3($dirname)
{
    if ($lstMp3 = glob($dirname . '/*.mp3')) {
        $file = $lstMp3[array_rand($lstMp3)];
        header('Content-Type: audio/mpeg');
        header('Content-length: ' . filesize($file));
        readfile($file);
    } else {
        http_response_code(404);
        die('No MP3 found');
    }
}

/**
 * Get all MP3s list as JSON
 *
 * @return string
 */
function getMP3List()
{
    $lstMp3 = array();
    foreach (glob(BASE_DIR . '/*', GLOB_ONLYDIR) as $d) {
        foreach (glob("$d/*.mp3") as $m) {
            $lstMp3[] = $m;
        }
    }

    return json_encode($lstMp3, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
}
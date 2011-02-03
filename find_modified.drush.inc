<?php
// $Id$

/**
 * @file
 *   Find modified code in your Drupal instance (differs from CVS)
 */

/**
 * Implementation of hook_drush_command().
 *
 * In this hook, you specify which commands your
 * drush module makes available, what it does and
 * description.
 *
 * Notice how this structure closely resembles how
 * you define menu hooks.
 *
 * @See drush_parse_command() for a list of recognized keys.
 *
 * @return
 *   An associative array describing your command(s).
 */
function find_modified_drush_command() {
  $items = array();

  $items['find-modified'] = array(
    'callback' => 'find_modified_find',
    'description' => "Find modified files what differ from the CVS (either core or contrib)",
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );
  $items['diff-modified'] = array(
    'callback' => 'find_modified_diff',
    'description' => "Find modified files what differ from the CVS (either core or contrib)",
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );
  return $items;
}

/**
 * Iterates through on the specified or the current directory and searches for modified files
 */
function find_modified_find() {
  $args = func_get_args();
  $path = isset($args[0]) ? $args[0] : drush_cwd();
  $objects = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($path), RecursiveIteratorIterator::SELF_FIRST);
  foreach ($objects as $name => $object) {
    if ($object->isFile()) {
      $ext = array_pop(explode('.', $object->getFilename()));
      if (in_array($ext, array('info', 'php', 'module', 'install', 'inc', 'js', 'css')) !== FALSE) {
        // Try to determine the CVS project, if the current dir does not contain .info file, let's try at max 3 level upper
        $cvs_proj = find_modified_get_cvs_proj($object->getPath());
        $real = $object->getRealPath();
        $rev = find_modified_get_cvs_rev($real);
        if (!empty($rev) && !empty($cvs_proj)) {
          if (find_modified_is_diff($object, $rev, $cvs_proj)) {
            print $object->getRealPath() . "\n";
          }
        }
      }
    }
  }
}

/**
 * Show the diff for a specific file
 */
function find_modified_diff() {
  $args = func_get_args();
  $object = new SplFileInfo($args[0]);
  $cvs_proj = find_modified_get_cvs_proj($object->getPath());
  $real = $object->getRealPath();
  $rev = find_modified_get_cvs_rev($real);

  if (!empty($rev) && !empty($cvs_proj)) {
    find_modified_is_diff($object, $rev, $cvs_proj, TRUE);
  }
  else {
    return drush_set_error(dt("Failed to detect revision or CVS project for !file", array('!file' => $args[0]))); 
  }
}

function find_modified_get_cvs_proj($path) {
  static $cache = array();
  if (!isset($cache[$path])) {
    $suffix = '';
    for ($i = 0; $i < 5; $i++) {
      $dir = new DirectoryIterator(realpath($path . $suffix));
      $infos = new RegexIterator($dir, '/^.+\.info$/i', RecursiveRegexIterator::GET_MATCH);
      foreach ($infos as $elem) {
        $filename = array_shift($elem);
        $infos = _find_modif_drupal_parse_info_file(realpath($path . $suffix) . '/' . $filename);
        if (!empty($infos['project'])) {
          $cache[$path] = trim($infos['project']);
        }
        else {
          $cache[$path] = trim(str_replace('.info', '', $filename));
        }
        break 2;
      }
      unset($dir);
      unset($infos);
      $suffix .= '/../';
    }
  }
  return strlen($cache[$path]) < 2 ? FALSE : $cache[$path];
}

function find_modified_get_cvs_rev($real) {
  // Attempt to determine CVS revision
  drush_shell_exec("cat " . $real .  " | grep // | head -n1 | cut -d \" \" -f 4");
  $rev = trim(array_shift(drush_shell_exec_output()));
  // "syntax check" on revision number
  $parts = explode(".", $rev);
  foreach ($parts as $num) {
    if (!is_numeric($num)) {
      $rev = '';
      break;
    }
  }
  return $rev;
}

function find_modified_is_diff($file, $rev, $cvs_proj, $show = FALSE) {
  static $cvs_temp = '';
  $output = FALSE;
  if (empty($cvs_temp)) {
    $cvs_temp = drush_tempdir();
    if (empty($cvs_temp) || realpath($cvs_temp) == '/') {
      return;
    }
  }
  drush_shell_exec("cd $cvs_temp && cvs -z6 -d:pserver:anonymous:anonymous@cvs.drupal.org:/cvs/drupal-contrib checkout -d ./orig -r $rev contributions/modules/$cvs_proj 2> /dev/null");
  $objects = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($cvs_temp), RecursiveIteratorIterator::SELF_FIRST);
  foreach ($objects as $f) {
    if ($file->getFilename() == $f->getFilename()) {
      // diff -bB means ignoring whitespace or empty line changes
      drush_shell_exec("diff -bB " . $f->getRealPath() . " " . $file->getRealPath());
      $diff = drush_shell_exec_output();
      if (!empty($diff)) {
        if ($show) {
          print implode("\n", $diff) . "\n";
        }
        $output = TRUE;
      }
      break;
    }
  }
  drush_shell_exec("rm -rf $cvs_temp/*");
  return $output;
}

  function _find_modif_drupal_parse_info_file($filename) {
    $info = array();
    $constants = get_defined_constants();

    if (!file_exists($filename)) {
      return $info;
    }

    $data = file_get_contents($filename);
    if (preg_match_all('
      @^\s*                           # Start at the beginning of a line, ignoring leading whitespace
      ((?:
      [^=;\[\]]|                    # Key names cannot contain equal signs, semi-colons or square brackets,
      \[[^\[\]]*\]                  # unless they are balanced and not nested
    )+?)
    \s*=\s*                         # Key/value pairs are separated by equal signs (ignoring white-space)
    (?:
    ("(?:[^"]|(?<=\\\\)")*")|     # Double-quoted string, which may contain slash-escaped quotes/slashes
    (\'(?:[^\']|(?<=\\\\)\')*\')| # Single-quoted string, which may contain slash-escaped quotes/slashes
    ([^\r\n]*?)                   # Non-quoted string
  )\s*$                           # Stop at the next end of a line, ignoring trailing whitespace
  @msx', $data, $matches, PREG_SET_ORDER)) {
    foreach ($matches as $match) {
      // Fetch the key and value string
      $i = 0;
      foreach (array('key', 'value1', 'value2', 'value3') as $var) {
        $$var = isset($match[++$i]) ? $match[$i] : '';
      }
      $value = stripslashes(substr($value1, 1, -1)) . stripslashes(substr($value2, 1, -1)) . $value3;

      // Parse array syntax
      $keys = preg_split('/\]?\[/', rtrim($key, ']'));
      $last = array_pop($keys);
      $parent = &$info;

      // Create nested arrays
      foreach ($keys as $key) {
        if ($key == '') {
          $key = count($parent);
        }
        if (!isset($parent[$key]) || !is_array($parent[$key])) {
          $parent[$key] = array();
        }
        $parent = &$parent[$key];
      }

      // Handle PHP constants.
      if (isset($constants[$value])) {
        $value = $constants[$value];
      }

      // Insert actual value
      if ($last == '') {
        $last = count($parent);
      }
      $parent[$last] = $value;
    }
  }

    return $info;
  }

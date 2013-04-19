<?php


/**
 * @file
 *
 */
define('D2DSEARCH_MAX_QUERY_LENGTH', 64);

function d2dsearch_main() {
  $build['main'] = array(
    '#title' => t('A small example to demonstrate search on remote hosts using D2D'),
    '#type' => 'fieldset',
  );
  $build['main'][] = drupal_get_form('d2dsearch_form_search');
  return $build;
}
function d2dsearch_form_search() {
  $form = array();
  $form['search_string'] = array(
    '#type' => 'textfield',
    '#title' => t('Search a remote host'),
//    '#description' => t('Address of the instance, e.g. http://www.example.com/xmlrpc.php'),
    '#size' => 40,
    '#maxlength' => D2DSEARCH_MAX_QUERY_LENGTH,
    '#required' => TRUE,
  );
  $form['submit'] = array(
    '#type' => 'submit', 
    '#value' => t('Search'),
    '#suffix' => '<div id="d2dsearch_progress"></div><div id="d2dsearch_results"></div>',
    //'#validate' => array('d2d_form_add_instance_submit_validate'),
    //'#submit' => array('d2d_form_add_instance_submit_submit'),
  );
//  $form['after_command_example'] = array(
//    '#value' => t("AJAX 'After': Click to put something after the div"),
//    '#type' => 'submit',
//    '#ajax' => array(
//      'callback' => 'd2dsearch_callback',
//    ),
//    '#suffix' => "<div id='after_div'>Something can be inserted after this</div>
//                  <div id='after_status'>'After' Command Status: Unknown</div>",
//  );
  return $form;
}

function d2dsearch_letsdoit($argument) {
  watchdog('d2dsearch', $argument . ' started');
  if (!_d2dsearch_lock_acquire('d2dsearch_letsdoit2', 6)) {
    watchdog('d2dsearch', $argument . ' giving up');
    return;
  }
  watchdog('d2dsearch', $argument . ' has acquired the lock');
  sleep(5);
  watchdog('d2dsearch', $argument . ' releasing the lock');
  lock_release('d2dsearch_letsdoit2');
}
function d2dsearch_form_search_submit($form, &$form_state) {
  $query_id = d2dsearch_query_start('d2dsearch_demo', $form_state['values']['search_string']);
  if (!_d2dsearch_check_query_id($query_id)) {
    drupal_set_message(t('Failed performing search.'));
  }
  else {
    $form_state['rebuild'] = TRUE;
    drupal_add_js(array('query_id' => $query_id), 'setting');
    drupal_add_js('http://ajax.googleapis.com/ajax/libs/jquery/1.9.1/jquery.min.js', 'external');
    drupal_add_js(drupal_get_path('module', 'd2dsearch') . '/js/search.js');
  }
}
function _d2dsearch_get_new_results($query_id, &$ids) {
  watchdog('d2dsearch', var_export($ids, TRUE));
  if (!_d2dsearch_lock_acquire('d2dsearch_query_cache', D2DSEARCH_ACQUIRE_TIMEOUT)) {
    return;
  }
  $db_result = db_query('SELECT qid as qid, instance_id as instance_id FROM {d2dsearch_query_cache} WHERE query_id=:query_id', array(':query_id' => $query_id));
  if ($record = $db_result->fetchObject()) {
    $qid = $record->qid;
  }
  else {
    $qid = NULL;
  }
  lock_release('d2dsearch_query_cache');
  if (is_null($qid)) {
    return '' ;
  }
  if (!_d2dsearch_lock_acquire('d2dsearch_result_cache', D2DSEARCH_ACQUIRE_TIMEOUT)) {
    return '';
  }
  if (empty($ids)) {
    $result = db_query('SELECT id, instance_id, result, timestamp FROM {d2dsearch_result_cache} WHERE qid=:qid', array(':qid' => $qid));
  }
  else {
    $result = db_query('SELECT id, instance_id, result, timestamp FROM {d2dsearch_result_cache} WHERE qid=:qid AND id NOT IN (:ids)', array(':qid' => $qid, ':ids' => $ids));
  }
  $records = $result->fetchAll();
  if (empty($records)) {
    return '';
  }
  $output = '';
  foreach ($records as $record) {
    watchdog('d2dsearch', $record->id);
    $ids[] = $record->id;
    $results = d2d_explode($record->result);
    if ($results === FALSE) {
      continue;
    }
    foreach ($results as $result) {
      $exploded_result = d2d_explode($result);
      if ($exploded_result !== FALSE && d2d_check_array($exploded_result, array('title' => 'is_string', 'time' => 'd2d_check_convert_int', 'name' => 'is_string'))) {
        //$output .= '<div style="border:1px solid #ccc; margin:5px; padding:5px">';
        $output .= '<div style="border:1px dashed #f00; margin:5px; padding:5px">';
        $output .= '<b>' . check_plain($exploded_result['name']) . '</b>';
        $output .= ' ';
        $output .= date('F j, Y', $exploded_result['time']);
        $output .= '<br/>';
        $output .= check_plain($exploded_result['title']);
        $output .= '</div>';
      }
    }
  }
  return $output;
}
function d2dsearch_get_results() {
  $ids_displayed = '';
  $new_results = '';
  if (isset($_POST['query_id']) && _d2dsearch_check_query_id($_POST['query_id'])) {
    if (isset($_POST['ids'])) {
      $ids = $_POST['ids'];
    }
    else {
      $ids = '';
    }
    $ids = d2d_explode($ids);
    if ($ids === FALSE) {
      $ids = array();
    }
    $new_results = _d2dsearch_get_new_results($_POST['query_id'], $ids);
    $ids_displayed = d2d_implode($ids);
  }
  $array = array(
    'ids' => $ids_displayed,
    'new_results' => $new_results,
  );
  drupal_json_output($array);
}
<?php

/**
 * @file
 *
 */

/**
 * Implements hook_d2d_secure_rpc().
 */
function d2dsearch_d2d_secure_rpc() {
  $methods = array();
  $methods['d2dsearch_process_query'] = array(
    'arguments' => array(
      'query_id' => '_d2dsearch_check_query_id',
      'type' => '_d2dsearch_check_query_type',
      'query' => '_d2dsearch_check_query',
    ),
    'callback' => '_d2dsearch_process_query',
    'description' => 'processes and forwards a query',
  );
  $methods['d2dsearch_process_result'] = array(
    'arguments' => array(
      'query_id' => '_d2dsearch_check_query_id',
      'result' => 'is_string',
    ),
    'callback' => '_d2dsearch_process_result',
    'description' => 'processes and forwards a result',
  );
  return $methods;
}
function _d2dsearch_process_query($arguments, $rpc_info) {
  background_process_start('_d2dsearch_process_query_helper', $arguments['query_id'], $arguments['type'], $arguments['query'], $rpc_info['id']);
  return 'SUCCESS';
}
function _d2dsearch_process_result($arguments, $rpc_info) {
  background_process_start('_d2dsearch_process_result_helper', $arguments['query_id'], $arguments['result'], $rpc_info['id']);
  return 'SUCCESS';
}
function _d2dsearch_lock_acquire($name, $max_wait) {
  if (lock_acquire($name)) {
    return TRUE;
  }
  $start_time = microtime(TRUE);
  while (microtime(TRUE) < $start_time + $max_wait) {
    if (!lock_wait($name, 1) && lock_acquire($name)) {
      return TRUE;
    }
  }
  return FALSE;
}
function _d2dsearch_check_query_id($query_id) {
  return is_string($query_id) && strlen($query_id) == D2DSEARCH_QUERY_ID_LENGTH && d2d_is_hex_string($query_id);
}
function _d2dsearch_check_query($query) {
  return is_string($query) && strlen($query) <= D2DSEARCH_QUERY_MAX_LENGTH;
}
function _d2dsearch_check_query_type($type) {
  return is_string($type) && strlen($type) <= D2DSEARCH_QUERY_TYPE_MAX_LENGTH;
}
function _d2dsearch_random_query() {
  return d2d_random_hex_string(D2DSEARCH_QUERY_ID_LENGTH);
}
function d2dsearch_query_start($type, $query, $do_local_search=TRUE) {
  $query_id = _d2dsearch_random_query();
  d2d_get_self_instance($my_id, $my_d2d_id, $my_url, $my_public_key);
  if (!_d2dsearch_process_query_helper($query_id, $type, $query, $my_id, $do_local_search)) {
    return FALSE;
  }
  return $query_id;
}
function _d2dsearch_query_forwarding_worker($qid, $query_id, $type, $query, $friends) {
  $n_friends = sizeof($friends);
  for ($i = 0; $i < $n_friends; $i++) {
    if (!_d2dsearch_lock_acquire('d2dsearch_query_batch', D2DSEARCH_ACQUIRE_TIMEOUT)) {
      return;
    }
    $result = db_query('SELECT id as id, instance_id as friend_id FROM {d2dsearch_query_batch} WHERE qid=:qid LIMIT 1', array(':qid' => $qid));
    if ($record = $result->fetchObject()) {
      $friend_id = $record->friend_id;
      $id = $record->id;
    }
    else {
      $id = NULL;
    }
    if (!is_null($id)) {
      db_delete('d2dsearch_query_batch')
        ->condition('id', $id)
        ->execute();
    }
    lock_release('d2dsearch_query_batch');
    if (is_null($id)) {
      return;
    }
    if (!array_key_exists($friend_id, $friends)) {
      continue;
    }
    $friend = $friends[$friend_id];
    $arguments = array(
      'query_id' => $query_id,
      'type' => $type,
      'query' => $query,
    );
    $res = d2d_call_secure_rpc($friend, 'd2dsearch_process_query', $arguments, $error_string);
    if ($res === FALSE) {
      watchdog('d2dsearch', check_plain($error_string));
    }
  }
}
function _d2dsearch_do_local_search($query_id, $type, $query) {
  $hooks = module_invoke_all('d2dsearch_query_type');
  if (!array_key_exists($type, $hooks)) {
    return;
  }
  $hook = $hooks[$type];
  $result = call_user_func($hook['callback'], $query);
  if (is_string($result)) {
    d2d_get_self_instance($my_id, $my_d2d_id, $my_url, $my_public_key);
    _d2dsearch_process_result_helper($query_id, $result, $my_id);
  }
}
function _d2dsearch_count_results($qid) {
  $result = db_query('SELECT count(*) as result_count FROM {d2dsearch_result_cache} WHERE qid=:qid', array(':qid' => $qid));
  if ($row = $result->fetchAssoc()) {
    return $row['result_count'];
  }
  else {
    return 0;
  }
}
function _d2dsearch_process_result_helper($query_id, $result, $instance_id) {
  if (!_d2dsearch_lock_acquire('d2dsearch_query_cache', D2DSEARCH_ACQUIRE_TIMEOUT)) {
    return;
  }
  $db_result = db_query('SELECT qid as qid, instance_id as instance_id FROM {d2dsearch_query_cache} WHERE query_id=:query_id', array(':query_id' => $query_id));
  if ($record = $db_result->fetchObject()) {
    $qid = $record->qid;
    $next_instance_id = $record->instance_id;
  }
  else {
    $qid = NULL;
  }
  lock_release('d2dsearch_query_cache');
  if (is_null($qid)) {
    return;
  }
  if (!_d2dsearch_lock_acquire('d2dsearch_result_cache', D2DSEARCH_ACQUIRE_TIMEOUT)) {
    return;
  }
  if (_d2dsearch_count_results($qid) >= D2DSEARCH_RESULT_CACHE_SIZE_PER_QUERY) {
    lock_release('d2dsearch_result_cache');
    return;
  }
  db_insert('d2dsearch_result_cache')->fields(array(
    'qid' => $qid,
    'instance_id' => $instance_id,
    'result' => $result,
    'timestamp' => d2d_get_time(),
  ))->execute();
  lock_release('d2dsearch_result_cache');
  d2d_get_self_instance($my_id, $my_d2d_id, $my_url, $my_public_key);
  if ($next_instance_id == $my_id) {
    watchdog('d2dsearch', 'received result: ' . $result);
    return;
  }
  $friend = d2d_get_instance2($next_instance_id);
  if (!$friend['is_friend']) {
    return;
  }
  $arguments = array(
    'query_id' => $query_id,
    'result' => $result,
  );
  watchdog('d2dsearch', 'sending result');
  $res = d2d_call_secure_rpc($friend, 'd2dsearch_process_result', $arguments, $error_string);
  if ($res === FALSE) {
    watchdog('d2dsearch', check_plain($error_string));
  }
}
function _d2dsearch_process_query_helper($query_id, $type, $query, $instance_id, $do_local_search=TRUE) {
  if (!_d2dsearch_lock_acquire('d2dsearch_query_cache', D2DSEARCH_ACQUIRE_TIMEOUT)) {
    return FALSE;
  }
  try {
    $qid = db_insert('d2dsearch_query_cache')->fields(array(
      'query_id' => $query_id,
      'type' => $type,
      'query' => $query,
      'instance_id' => $instance_id,
      'timestamp' => d2d_get_time(),
    ))->execute();
  }
  catch (Exception $e) {
    $qid = NULL;
  }
  lock_release('d2dsearch_query_cache');
  if (is_null($qid)) {
    return FALSE;
  }
  //watchdog('d2dsearch', var_export(array('$query_id' => $query_id, '$type' => $type, '$query' => $query, '$instance_id' => $instance_id), TRUE));
  $friends = d2d_get_friends(FALSE); // TODO: select specific friends, e.g. from a group
  unset($friends[$instance_id]);
  if (!_d2dsearch_lock_acquire('d2dsearch_query_batch', D2DSEARCH_ACQUIRE_TIMEOUT)) {
    return;
  }
  $db_query = db_insert('d2dsearch_query_batch')->fields(array('qid', 'instance_id'));
  foreach ($friends as $friend_id => $record) {
    $db_query->values(array('qid' => $qid, 'instance_id' => $friend_id));
  }
  $db_query->execute();
  lock_release('d2dsearch_query_batch');
  $n_friends = sizeof($friends);
  $n_query_forwarding_worker = min($n_friends, D2DSEARCH_NUMBER_OF_QUERY_FORWARDING_WORKER);
  for ($i = 0; $i < $n_query_forwarding_worker; $i++) {
    background_process_start('_d2dsearch_query_forwarding_worker', $qid, $query_id, $type, $query, $friends);
  }
  if ($do_local_search) {
    _d2dsearch_do_local_search($query_id, $type, $query);
  }
  return TRUE;
}
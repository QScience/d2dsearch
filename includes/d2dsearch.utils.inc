<?php

/**
 * @file Utility functions to validate variables, to acquire db lock, etc.
 */

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

function _d2dsearch_count_results($qid) {
  $result = db_query('SELECT count(*) as result_count FROM {d2dsearch_result_cache} WHERE qid=:qid', array(':qid' => $qid));
  if ($row = $result->fetchAssoc()) {
    return $row['result_count'];
  }
  else {
    return 0;
  }
}
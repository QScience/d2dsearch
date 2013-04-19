jQuery(document).ready(function(){ var counter = 0; var ids = "";
(function worker() {
  $.ajax({
    url: '?q=admin/d2d/search/get_result',
    data: {'query_id':Drupal.settings.query_id,'ids':ids},
    type: 'POST',
    success: function(data) {
      ids = data.ids;
	  //$("#d2dsearch_results").append('<div>bla</div>');
	  $("#d2dsearch_results").append(data.new_results);
	  counter += 5;
	  $("#d2dsearch_progress").html("This search " + counter + "% done.");
    },
    error: function() {
      //alert('failure');
	  counter = 100;
	  $("#d2dsearch_progress").html("This search " + counter + "% done.");
	},
    complete: function() {
      // Schedule the next request when the current one's complete
	  if (counter < 100) {
      	setTimeout(worker, 500);
	  }
    }
  });
})()
});
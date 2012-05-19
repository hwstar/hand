#!/usr/bin/php
<?php
require_once("errorlog.php");
require_once("han.php");
require_once("hanelement.php");

	$hanhost = 'phones';


	$host = 'gw2';
	$user = 'hantest';
	$pw = 'test';
	$db = 'hantest';

	$han = new Han($hanhost);

	$el = new Errorlog;
	$el->set_debug_level(DEBUG_UNEXPECTED);
	$el->set_timestamps(TRUE);

	$link = mysql_connect($host, $user, $pw);
	if (!$link) {
    	$el->fatal('Could not connect: %s', mysql_error());
	}
	$he = new Hanelement($el, $han, "ATTICTEMPRLYCH0");

#	printf("INPUTS:\n");
#	print_r($he->get_inputs());
#	printf("\n");

#	printf("COMMANDS:\n");
#	print_r($he->get_commands());
#	printf("\n");

#	printf("Device address: %d\n", $he->get_device());
#	printf("Device channel: %d\n", $he->get_channel());

	$instr = array("COMMAND" => "POLL", "STATE" => "");

	$res = $he->transact($instr);
	if($res === FALSE){
		printf("Error in transaction: %s\n", $he->error_string());
	}
	else{
		print_r($res);
	}



?>


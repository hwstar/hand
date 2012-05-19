<?php
#
# errorlog.php
#
# 


define("DEBUG_NONE",0);
define("DEBUG_UNEXPECTED", 1);
define("DEBUG_EXPECTED", 2);
define("DEBUG_STATUS", 3);
define("DEBUG_ACTION", 4);
define("DEBUG_ALL",5);


class Errorlog
{
protected $_fp;
protected $_debuglvl;
protected $_with_timestamp;



public function __construct($logfile = 'php://stderr', $timestamp = FALSE, $debuglvl = DEBUG_NONE)
{
	$this->_fp = fopen($logfile,'w');
	if(is_null($this->_fp)){
		die("Can't open log stream\n");
	}
	$this->_debuglvl = $debuglvl;
	$this->_with_timestamp = $timestamp;	

}

# Set the debug level

public function set_debug_level($level)
{
	$this->_debuglvl = $level;
}

# Set time stamp mode

public function set_timestamps($onoff)
{
	$this->_with_timestamp = $onoff;
}

# Print error message using printf format to log stream and exit

public function fatal()
{
	$na = func_num_args();
	$args = func_get_args();
	if($this->_with_timestamp){
		fprintf($this->_fp,"%s: ",strftime("%c"));
	}

	fputs($this->_fp, "Fatal Error: ");
	if(!$na){
		fputs($this->_fp, "Errorlog::fatal() called with no arguments!\n");
		fflush($this->_fp);
		exit(1);
	}
	if($na > 1){
		$errstr = vsprintf($args[0], array_slice($args, 1));
	}
	else{
		$errstr = $args[0];
	}
	fputs($this->_fp, $errstr); 
	fputs($this->_fp, "\n");
	fflush($this->_fp);
	exit(1);	
}

# Print debug message to log stream and return

public function debug()
{
	$na = func_num_args();
	$args = func_get_args();
	$level = array_shift($args);

	if($na < 2)
		return;

	if($level > $this->_debuglvl){
		return;
	}

	$format = array_shift($args);

	if($this->_with_timestamp){
		fprintf($this->_fp,"%s: ",strftime("%c"));
	}

	switch($level){
		case DEBUG_UNEXPECTED:
			$lstr = "DEBUG UNEXPECTED";
			break;

		case DEBUG_EXPECTED:
			$lstr = "DEBUG EXPECTED";
			break;

		case DEBUG_STATUS:
			$lstr = "DEBUG STATUS";
			break;

		case DEBUG_ACTION:
			$lstr = "DEBUG ACTION";
			break;

		case DEBUG_ALL:
		default:
			return;
	}
	fprintf($this->_fp, "%s: ", $lstr);
	$dmsg = vsprintf($format, $args);
	fprintf($this->_fp, "%s\n", $dmsg);
}

public function __destruct()
{
	if(!is_null($this->_fp)){
		fflush($this->_fp);
		fclose($this->_fp);
	}
}


} # End class Log

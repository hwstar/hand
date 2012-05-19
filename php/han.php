<?php

class Han
{

protected $_fp;
protected $_reason;

# Constructor

public function __construct($host)
{
	$this->_reason = "";

	$colon = strchr($host, ":");

	if(!$colon){
		$res = @dns_get_record($host, DNS_AAAA);
		if($res){
			$hn = sprintf("[%s]", $res[0]['ipv6']); # IPV6 name
		}
		else{
			$hn = $host; # IPV4
		}
	}
	else{
		$hn = sprintf("[%s]", $host); #IPV6 address
	}

	$this->_fp = @fsockopen("tcp://$hn:1129");

}

# Destructor

public function __destruct()
{
	if($this->_fp){
		fclose($this->_fp);
	}
}

#
# Return a signed 16 bit quantity from two byte values
#

public function tosigned16($low, $high)
{
	$res = $high*256 + $low;
	
	if($res > 32767){
		$res = (65536 - $res)*-1;
	}
	return $res;		
}

# Send a raw transaction

private function rawtx($cmdstr)
{
	if($this->_fp === FALSE){
		$this->_reason = "Connect failed";
		return FALSE;
	}

	fputs($this->_fp, $cmdstr);

	stream_set_timeout($this->_fp, 10, 0);

	# Get the response
	$res = fgets($this->_fp, 50);

	# Test for time out
	$info = stream_get_meta_data($this->_fp);
	if ($info['timed_out']){
		$this->_reason = "Timed out waiting for response";
		return FALSE;
	}

	$rc = substr($res, 0, 2);	


	if($rc != "RS"){
		$this->_reason = sprintf("Received error %s from node", $rc);
		return FALSE;
	}

	return $res;


}


# Send the command

public function transact()
{
	
	# Build the command string

	$na = func_num_args();

	if(($na < 3) ||($na > 19)){
		$this->_reason = "Invalid number of parameters";
		return FALSE;
	}

	$cmdstr = sprintf("CA%02X%02X", func_get_arg(0), func_get_arg(1));
	
	if($na >= 3){
		for($i = 2 ; $i < $na; $i++){
			$cmdstr = $cmdstr . sprintf("%02X",func_get_arg($i));
		}
	}

	#Send a the command

	$res = $this->rawtx($cmdstr);

	if(is_null($res))
		return FALSE;

	# Convert returned data to an array

	for($i = 6; $i < strlen($res)-1 ; $i += 2){
		$rp[($i-6)/2] = hexdec(substr($res, $i, 2)); 
	}
	return $rp;
}

# Parse and transact formatting input and output fields according to a format string
#
# Arguments
#
# node address
# format string 
# input parameters (variable argument list). If first input parameter is an array, that will be used instead of all of the other arguments.
#
# Return value
#
# Array of return values according to format string, or NULL if error
#
# Format strings start with 0: followed by a 2 digit hex command and then zero or more characters indicating the field types.
# Field types are:
#
# B - 8 bit byte
# I - 16 bit signed integer
# U - 16 bit unsigned integer
#
# If a parameter does not exist for a field specified in the format string, a value of 0 will be sent to the node by default as a place holder for the return parameter.




public function ptransact()
{
	$na = func_num_args();

	if($na < 2){
		$this->_reason = "Must have 2 or more parameters";
		return FALSE;
	}

	$format = func_get_arg(1);
	if("0:" != substr($format, 0, 2)){ # check for good header
		$this->_reason = "Malformed header";
		return FALSE;
	}

	$command = substr($format, 2, 2); # extract command
	if(($command == FALSE)||(strlen($command) != 2)){
		$this->_reason = "Malformed command";
		return FALSE;
	}

	$cmdstr = sprintf("CA%02X%s", func_get_arg(0), $command);

	# process parameters

	for($i = 4, $p = 2; $i < strlen($format); ){
		if(is_array(func_get_arg(2))){
			$a = func_get_arg(2);
			$v = $a[($p - 2)];
		}
		else{
			$v = func_get_arg($p);
		}

		if((is_numeric($v) === FALSE)){
			$v = 0;
		}
		switch(substr($format, $i, 1)){
			case 'B': # Byte
				$cmdstr = $cmdstr . sprintf("%02X", $v);
				break;

			case 'I': # 16 bit integer
			case 'U': # 16 bit unsigned
				$cmdstr = $cmdstr . sprintf("%02X%02X", $v & 0xFF, ($v >> 8) & 0xFF);
				break;
 
			default:
				$this->_reason = "Unrecognized format code";
				return NULL;
		}
		$p++;
		$i++;
				
	}
	$res = $this->rawtx($cmdstr);

	if(is_null($res)){
		return FALSE;
	}


	# Break out the return parameters using the format string

	$rvals = array();
	for($i = 4, $p = 6; $i < strlen($format); ){
		switch(substr($format, $i, 1)){
			case 'B': # Byte
				array_push($rvals,  hexdec(substr($res, $p, 2)));
				$p+=2;
				break;

			case 'I': # 16 bit Integer
				$low = hexdec(substr($res, $p, 2));
				$p+=2;
				$high = hexdec(substr($res, $p, 2));
				$p+=2;
				array_push($rvals, $this->tosigned16($low, $high));
				break;
			
			case 'U': # 16 bit Unsigned
				$low = hexdec(substr($res, $p, 2));
				$p+=2;
				$high = hexdec(substr($res, $p, 2));
				$p+=2;
				array_push($rvals, $high * 256 + $low);
				break;

			default:
				$this->_reason = "Problem processing return values";
				return FALSE;
		}
		$i++;
	}
	return $rvals;
}

# Return a string describing the error

public function error_string()
{
	return $this->_reason;
}


} # End class

?>


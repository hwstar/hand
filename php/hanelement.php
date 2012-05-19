<?php
#
# Hanelement.php
#

require_once("mathexp.php");

class Hanelement
{
	private $reason;
	private $el;
	private $han;
	private $math;
	private $props;

# Constructor

public function __construct($el, $han, $ecode, $db = 'hantest')
{
	$this->el = $el;
	$this->han = $han;

	$this->math = new Mathexp; 

	# Get element

	$query = sprintf("SELECT * FROM %s . element WHERE Ecode='%s'", $db,'ATTICTEMPRLYCH0');

	$this->el->debug(DEBUG_ACTION,"Query: %s", $query);

	$res = mysql_query($query);

	if(!$res){
		$this->el->fatal(' Query failed: %s', mysql_error());
	}
	$row = mysql_fetch_row($res);

	$this->props['fcode'] = $row[1];
	$this->props['device'] = $row[2];
	$this->props['channel'] = $row[3];
	$this->props['units'] = $row[4];
	$this->props['parameter'] = $row[5];
	
	# Get function

	$query = sprintf("SELECT * FROM %s . function WHERE Fcode='%s'", $db, $this->props['fcode']);

	$this->el->debug(DEBUG_ACTION,"Query: %s", $query);

	$res = mysql_query($query);

	if(!$res){
		$this->el->fatal(' Query failed: %s', mysql_error());
	}
	$row = mysql_fetch_row($res);

	$this->props['ccode'] = $row[1];
	$this->props['scode'] = $row[2];

	#Generate ilist array

	$this->props['ilist'] = array();
	if($row[3]){
		$inputs = explode(',', $row[3]);
		foreach($inputs as $i){
			$kv = explode('=', $i);
			$this->props['ilist']["$kv[0]"] = $kv[1];
		}
	}

	# Generate clist array

	$this->props['clist'] = array();
	if($row[4]){
		$commands = explode(',', $row[4]);
		foreach($commands as $cmd){
			$kv = explode('=', $cmd);
			$this->props['clist']["$kv[0]"] = $kv[1];
		}
	}

	# Get command format

	$query = sprintf("SELECT * FROM %s . commands WHERE Ccode='%s'", $db, $this->props['ccode']);

	$this->el->debug(DEBUG_ACTION,"Query: %s", $query);

	$res = mysql_query($query);

	if(!$res){
		$this->el->fatal(' Query failed: %s', mysql_error());
	}
	$row = mysql_fetch_row($res);

	$this->props['cmdfmt'] = $row[1];

	# Get scaling function

	$query = sprintf("SELECT * FROM %s . scaling WHERE Scode='%s'", $db, $this->props['scode']);

	$this->el->debug(DEBUG_ACTION,"Query: %s", $query);

	$res = mysql_query($query);

	if(!$res){
		$this->el->fatal('Query failed: %s', mysql_error());
	}

	$row = mysql_fetch_row($res);

	$this->props['exp'] = $row[1];


}

# Destructor

public function __destruct()
{
	unset($this->math);
}	 

# Return input list with CHANNEL excluded

public function get_inputs()
{

	$a = $this->props['ilist'];
	if(isset($a['CHANNEL'])){
		unset($a['CHANNEL']);
	}
	return array_keys($a);
}

# Return list of valid commands

public function get_commands()
{
	return array_keys($this->props['clist']);

}

# Return device address

public function get_device()
{
	return $this->props['device'];
}

# Return channel number

public function get_channel()
{
	return $this->props['channel'];
}

# Validate an instruction list

public function validate($instr)
{
	$inputs = $this->get_inputs();
	$na = 0;

	if(is_null($instr) && !is_null($this->props['ilist']))
		return TRUE;

	if(count($instr) > 16){
		return FALSE;
	}

	foreach($instr as $k => $v){
		if($k == 'COMMAND'){
			if(array_key_exists($v, $this->props['clist']) === FALSE)
				return FALSE;
		}
	}

	foreach($inputs as $k => $v){
		if(array_key_exists($v, $instr) === FALSE){
			break;
		}
		$na++;
	}
	if($na != count($inputs)){
		return FALSE;
	}

	return TRUE;
}


# Do a transaction with the instruction list passed in.
# The instruction list is an array of input fields as keys and values as values.


public function transact($instr)
{
	if($this->validate($instr) === FALSE){
		$this->reason = 'Validation Failed';
		return FALSE;
	}

	$ilist = $this->props['ilist'];
	$p = array_fill(0, 16, 0);

	if(!is_null($instr)){
		if(isset($ilist['CHANNEL'])){
			$p[$ilist['CHANNEL']] = $this->props['channel'];
		}
		foreach($instr as $k => $v){
			if($k == 'COMMAND'){
				$p[$ilist["$k"]] = $this->props['clist']["$v"];
			}
			else{
				$p[$ilist["$k"]] = (is_numeric($v))? $v : 0;		
			}
		}
	}
	$res = $this->han->ptransact($this->props['device'], $this->props['cmdfmt'], $p);
	if($res === FALSE){
		$this->reason = $this->han->error_string();
		return FALSE;
	}

	$dp = $this->math->parse($res, $this->props['exp']);
	if($dp === FALSE){
		$this->reason = $this->math->error_string();
	}
	return $dp;
}

# Return a string describing an error condition

public function error_string()
{
	return $this->reason;
}




} # End class Hanelement
?>


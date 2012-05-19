
<?php
class Mathexp
{
protected $_reason;
 

#
# Do a math function via an expression on an array of return values
#

public function parse($r, $exp)
{
	//Signs
	//Can't assign stuff, test for equality or test for identity
	$bl_signs = array("=");

	//Language constructs
	$bl_constructs = array("print","echo","require","include","if","else",
	"while","for","switch","exit","break","fopen");   

	//Functions
	$funcs = get_defined_functions();
	$funcs = array_merge($funcs['internal'], $funcs['user']);

	//Functions allowed       
	$whitelist = array("pow","exp","sqrt","abs","sin","cos","tan","asin","acos","atan","atan2","sinh","cosh",
	"tanh","asinh","acosh","atanh","pi","log","log10","ceil","floor","round","max","min","fmod");
   
	//Remove whitelist elements
	foreach($whitelist as $f) {
        	unset($funcs[array_search($f, $funcs)]);   
	}
	//Append '(' to prevent confusion (e.g. array() and array_fill())
	foreach($funcs as $key => $val) {
		$funcs[$key] = $val."(";
	}
	$blacklist = array_merge($bl_signs, $bl_constructs, $funcs);
   
	//Check
	foreach($blacklist as $notallowed) {
		if(strpos($exp,$notallowed) !== false) {
			$this->_reason = sprintf("%s not allowed in math expression", $notallowed);
			return FALSE;
		}
	}

	//Eval
    	$res = @eval($exp);
	if(($res === FALSE)||(is_null($res))){
		$this->_reason = "function parse error";
		return FALSE;
	}
	if(!is_string($res) && is_nan($res)){
		$this->_reason = "returned NaN";
		return FALSE;
	}
	return $res; 
}


# Return a string describing the error

public function error_string()
{
	return $this->_reason;
}



} // End class Mathexp	


?>

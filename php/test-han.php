#!/usr/bin/php
<?php
require_once("han.php");
require_once("mathexp.php");

$han = new Han("phones");
$math = new Mathexp;



$rp = $han->ptransact(6, "0:12BBBI", 1, 0, 0, 0);

if(!$rp){
	printf("Error: %s\n", $han->error_string());
	exit(1);
}


$t = $math->parse($rp, 'return($r[3]/(1.0 * $r[1]));');

if(!$t){
	printf("Error: %s\n", $math->error_string());
	exit(1);
}

printf("Temp: %f\n", $t);

# Exit good

exit(0);
?>

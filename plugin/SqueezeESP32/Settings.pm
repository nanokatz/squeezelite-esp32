package Plugins::SqueezeESP32::Settings;
use base qw(Slim::Web::Settings);

use strict;

use Slim::Utils::Prefs;
use Slim::Utils::Log;

my $log = logger('plugin.squeezeesp32');

sub name {
	return 'PLUGIN_SQUEEZEESP32';
}

sub page {
	return 'plugins/SqueezeESP32/settings/basic.html';
}

sub prefs {
	return (preferences('plugin.squeezeesp32'), qw(width spectrum_scale));
}

sub handler {
	my ($class, $client, $params, $callback, @args) = @_;
	
	$callback->($client, $params, $class->SUPER::handler($client, $params), @args);
}

	
1;
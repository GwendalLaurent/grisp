-module(grisp_app).

-behaviour(application).

-include("grisp.hrl").

% API
-export([start/2]).
-export([stop/1]).

%--- API -----------------------------------------------------------------------

start(_Type, _Args) ->
    Devices = application:get_env(grisp, devices, []),
    {ok, Sup} = grisp_sup:start_link(),
    DeviceState = grisp_device:setup(Devices),
    {ok, Sup, DeviceState}.

stop(DeviceState) ->
    grisp_device:teardown(DeviceState).

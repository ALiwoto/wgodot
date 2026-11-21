# AudioStreamGroup delayed children

`delay_range` holds a group's mixing before its selected children contribute.
By default, children start after that delay. Set `start_children_before_delay`
to prepare and start their playbacks when the parent starts instead. The parent
still holds their sample clocks until its delay expires; this supports sources
that prepare an intro and a scheduled loop together. Seeking preserves choices
and applies the same lifecycle. The setting defaults to false.

The live audio observer reports these real children as pending through the delayed
ancestor. Each group's delay remains on that group's record, in its own clock
units. It does not fabricate unsampled random branches or flatten distinct clocks.

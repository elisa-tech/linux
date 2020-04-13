EDAC Injection Device
===========================

This device lets you inject errors into the Linux EDAC system for
testing userland software that interacts with EDAC, since causing your
own memory errors is hard.

The interface for injecting errors appears in the sysfs file system in
the same place as the EDAC driver's interface::

	/sys/devices/system/edac/mc/mc<n>/..

The files for injection all start with "inject_", they are:

- inject_ce - correctable error
- inject_de - deferred error
- inject_fe - fatal error
- inject_ie - informative error
- inject_ue - uncorrectable error

Writing a number besides zero to these will result in that many errors
being injected.  Each write injects new errors.  Reading the value
returns the number of pending errors to be injected that have not
yet completed.

In addition to these, some data is passed along with the error to show
where it occurred.  These values mostly default to zero or empty, but
they can be set in the following write only values:

- inject_low - low layer
- inject_mid - mid layer
- inject_msg - error message string. This defaults to "dummy <error
  type>" where "<error type>" is "correctable error", "uncorrectable
  error", etc.
- inject_oip - offset in page
- inject_other_detail - Other details string
- inject_pfn - page frame number
- inject_syndrome - symdrome
- inject_top - top layer

Note that these values are taken when the count is written, so you are
free to set up the info, write an error count, change the info, write
another error count, etc.

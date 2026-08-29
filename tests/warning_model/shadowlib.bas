' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' A library whose function name a caller also defines locally, for the
' local-shadows-library tier.
library shadowlib
    function start_server(port)
        return "library:" + string(port)
    end function
end library

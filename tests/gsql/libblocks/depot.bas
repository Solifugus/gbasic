' A second library with a `database shop` of its own: two databases, not one.
library depot
    load gsql

    database shop( file: "depot.gsql", create: true )

    sql make on shop
        create table bin (code string primary key)
    end sql

    sql add on shop
        insert into bin (code) values (:code)
    end sql

    sql codes on shop
        select code from bin order by code
    end sql

    function setup()
        make.exec()
    end function

    function put(code)
        add.exec()
    end function

    function all_codes()
        out = []
        for each row in codes
            append(out, row.code)
        end for
        return out
    end function
end library

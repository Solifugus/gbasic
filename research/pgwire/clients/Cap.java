import java.sql.*;
public class Cap { public static void main(String[] a) throws Exception {
  Connection c = DriverManager.getConnection("jdbc:postgresql://127.0.0.1:"+a[0]+"/pgcap?sslmode=disable","solifugus","");
  PreparedStatement p = c.prepareStatement("select id, name, since from customer where id = ?"); p.setInt(1,1); p.executeQuery().close();
  c.createStatement().executeQuery("select total from invoice").close();
  if (a.length > 1) { // what a BI tool asks: DatabaseMetaData
    DatabaseMetaData m = c.getMetaData();
    m.getDatabaseProductVersion(); m.getSchemas().close(); m.getCatalogs().close();
    m.getTables(null,"public","%",new String[]{"TABLE","VIEW"}).close();
    m.getColumns(null,"public","customer","%").close();
    m.getPrimaryKeys(null,"public","customer").close();
    m.getImportedKeys(null,"public","invoice").close();
    m.getIndexInfo(null,"public","invoice",false,true).close();
    m.getTypeInfo().close();
    ResultSet r=c.createStatement().executeQuery("select * from invoice"); r.getMetaData().isNullable(1); r.getMetaData().getTableName(1); r.close();
  }
  c.close(); } }

package com.example.tooltest;
import android.app.Activity;
import android.os.Bundle;
import android.widget.*;
public class MainActivity extends Activity {
  public static String secret = "hello-static";
  private int counter = 0;
  static { System.loadLibrary("tool"); }
  public String greet(String who, int times){ counter++; return "Hi " + who + " x" + times + " (#" + counter + ")"; }
  public int add(int a, int b){ return a + b; }
  public void logIt(String s){ android.util.Log.i("ToolTest", s); }
  @Override protected void onCreate(Bundle b){
    super.onCreate(b);
    LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.VERTICAL);
    TextView t = new TextView(this); t.setText("libtool test app - tap TOOL button");
    final EditText e = new EditText(this); e.setHint("name");
    Button btn = new Button(this); btn.setText("Call greet()");
    final TextView out = new TextView(this);
    btn.setOnClickListener(new android.view.View.OnClickListener(){
      @Override public void onClick(android.view.View v){
        out.setText(greet(e.getText().toString(), add(2,3)));
        logIt(out.getText().toString());
      }
    });
    l.addView(t); l.addView(e); l.addView(btn); l.addView(out);
    setContentView(l);
  }
}

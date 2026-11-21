package com.example.tooltest;
import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
public class MainActivity extends Activity implements View.OnClickListener {
    public static String secret = "hello-static";
    private int counter = 0;
    private EditText input;
    private TextView output;
    public String greet(String who, int times) { counter++; return "Hi " + who + " x" + times + " (#" + counter + ")"; }
    public int add(int a, int b) { return a + b; }
    public void logIt(String s) { android.util.Log.i("ToolTest", s); }
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (Build.VERSION.SDK_INT < 29) {
            requestPermissions(new String[]{android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, 7);
        }
        System.loadLibrary("tool");
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        TextView t = new TextView(this);
        t.setText("libtool test app - tap TOOL button");
        input = new EditText(this); input.setHint("name");
        Button btn = new Button(this); btn.setText("Call greet()");
        output = new TextView(this);
        btn.setOnClickListener(this);
        l.addView(t); l.addView(input); l.addView(btn); l.addView(output);
        setContentView(l);
    }
    @Override public void onClick(View v) {
        output.setText(greet(input.getText().toString(), add(2, 3)));
        logIt(output.getText().toString());
    }
}

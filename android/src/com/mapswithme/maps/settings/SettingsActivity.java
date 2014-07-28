package com.mapswithme.maps.settings;

import android.app.ActionBar;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageManager.NameNotFoundException;
import android.net.MailTo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.preference.CheckBoxPreference;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.Preference.OnPreferenceClickListener;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.animation.AlphaAnimation;
import android.view.inputmethod.InputMethodManager;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import com.mapswithme.maps.MWMApplication;
import com.mapswithme.maps.R;
import com.mapswithme.util.UiUtils;
import com.mapswithme.util.Yota;
import com.mapswithme.util.statistics.Statistics;

public class SettingsActivity extends PreferenceActivity
{
  public final static String ZOOM_BUTTON_ENABLED = "ZoomButtonsEnabled";
  private static final String ABOUT_ASSET_URL = "file:///android_asset/about.html";

  private Preference mStoragePreference = null;
  private StoragePathManager mPathManager = new StoragePathManager();

  @SuppressWarnings("deprecation")
  @Override
  protected void onCreate(Bundle savedInstanceState)
  {
    super.onCreate(savedInstanceState);

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB)
    {
      // http://stackoverflow.com/questions/6867076/getactionbar-returns-null
      final ActionBar bar = getActionBar();
      if (bar != null)
        bar.setDisplayHomeAsUpEnabled(true);
    }

    addPreferencesFromResource(R.xml.preferences);

    final Activity parent = this;

    mStoragePreference = findPreference(getString(R.string.pref_storage_activity));
    mStoragePreference.setOnPreferenceClickListener(new OnPreferenceClickListener()
    {
      @Override
      public boolean onPreferenceClick(Preference preference)
      {
        if (isDownloadingActive())
        {
          new AlertDialog.Builder(parent)
              .setTitle(parent.getString(R.string.downloading_is_active))
              .setMessage(parent.getString(R.string.cant_change_this_setting))
              .setPositiveButton(parent.getString(R.string.ok), new DialogInterface.OnClickListener()
              {
                @Override
                public void onClick(DialogInterface dlg, int which)
                {
                  dlg.dismiss();
                }
              })
              .create()
              .show();

          return false;
        }
        else
        {
          parent.startActivity(new Intent(parent, StoragePathActivity.class));
          return true;
        }
      }
    });

    final ListPreference lPref = (ListPreference) findPreference(getString(R.string.pref_munits));

    lPref.setValue(String.valueOf(UnitLocale.getUnits()));
    lPref.setOnPreferenceChangeListener(new OnPreferenceChangeListener()
    {
      @Override
      public boolean onPreferenceChange(Preference preference, Object newValue)
      {
        UnitLocale.setUnits(Integer.parseInt((String) newValue));
        return true;
      }
    });


    final CheckBoxPreference allowStatsPreference = (CheckBoxPreference) findPreference(getString(R.string.pref_allow_stat));
    allowStatsPreference.setChecked(Statistics.INSTANCE.isStatisticsEnabled(this));
    allowStatsPreference.setOnPreferenceChangeListener(new OnPreferenceChangeListener()
    {
      @Override
      public boolean onPreferenceChange(Preference preference, Object newValue)
      {
        Statistics.INSTANCE.setStatEnabled(getApplicationContext(), (Boolean) newValue);
        return true;
      }
    });

    final CheckBoxPreference enableZoomButtons = (CheckBoxPreference) findPreference(getString(R.string.pref_zoom_btns_enabled));
    enableZoomButtons.setChecked(MWMApplication.get().nativeGetBoolean(ZOOM_BUTTON_ENABLED, true));
    enableZoomButtons.setOnPreferenceChangeListener(new OnPreferenceChangeListener()
    {
      @Override
      public boolean onPreferenceChange(Preference preference, Object newValue)
      {
        MWMApplication.get().nativeSetBoolean(ZOOM_BUTTON_ENABLED, (Boolean) newValue);
        return true;
      }
    });

    final Preference pref = findPreference(getString(R.string.pref_about));
    pref.setOnPreferenceClickListener(new OnPreferenceClickListener()
    {
      @Override
      public boolean onPreferenceClick(Preference preference)
      {
        onAboutDialogClicked(SettingsActivity.this);
        return true;
      }
    });

    yotaSetup();
  }

  @SuppressWarnings("deprecation")
  private void storagePathSetup()
  {
    PreferenceScreen screen = getPreferenceScreen();
    if (Yota.isYota())
      screen.removePreference(mStoragePreference);
    else if (mPathManager.hasMoreThanOneStorage())
      screen.addPreference(mStoragePreference);
    else
      screen.removePreference(mStoragePreference);
  }

  @SuppressWarnings("deprecation")
  private void yotaSetup()
  {
    final Preference yopPreference = findPreference(getString(R.string.pref_yota));
    if (!Yota.isYota())
      getPreferenceScreen().removePreference(yopPreference);
    else
    {
      yopPreference.setOnPreferenceClickListener(new OnPreferenceClickListener()
      {
        @Override
        public boolean onPreferenceClick(Preference preference)
        {
          SettingsActivity.this.startActivity(new Intent(Yota.ACTION_PREFERENCE));
          return true;
        }
      });
    }
  }

  @Override
  protected void onStart()
  {
    super.onStart();

    Statistics.INSTANCE.startActivity(this);
  }

  @Override
  protected void onStop()
  {
    super.onStop();

    Statistics.INSTANCE.stopActivity(this);
  }

  @Override
  protected void onResume()
  {
    super.onResume();
    BroadcastReceiver receiver = new BroadcastReceiver()
    {
      @Override
      public void onReceive(Context context, Intent intent)
      {
        storagePathSetup();
      }
    };
    mPathManager.startExternalStorageWatching(this, receiver, null);
    storagePathSetup();
  }

  @Override
  protected void onPause()
  {
    super.onPause();
    mPathManager.stopExternalStorageWatching();
  }

  @Override
  public boolean onOptionsItemSelected(MenuItem item)
  {
    if (item.getItemId() == android.R.id.home)
    {
      final InputMethodManager imm = (InputMethodManager) getSystemService(Activity.INPUT_METHOD_SERVICE);
      imm.toggleSoftInput(InputMethodManager.HIDE_IMPLICIT_ONLY, 0);
      onBackPressed();
      return true;
    }
    else
      return super.onOptionsItemSelected(item);
  }

  public void onAboutDialogClicked(Activity parent)
  {
    final LayoutInflater inflater = LayoutInflater.from(parent);
    final View alertDialogView = inflater.inflate(R.layout.about, null);
    final WebView myWebView = (WebView) alertDialogView.findViewById(R.id.webview_about);

    myWebView.setWebViewClient(new WebViewClient()
    {
      @Override
      public void onPageFinished(WebView view, String url)
      {
        super.onPageFinished(view, url);
        UiUtils.show(myWebView);

        final AlphaAnimation aAnim = new AlphaAnimation(0, 1);
        aAnim.setDuration(750);
        myWebView.startAnimation(aAnim);
      }

      @Override
      public boolean shouldOverrideUrlLoading(WebView v, String url)
      {
        if (MailTo.isMailTo(url))
        {
          MailTo parser = MailTo.parse(url);
          Context ctx = v.getContext();
          Intent mailIntent = CreateEmailIntent(parser.getTo(),
              parser.getSubject(),
              parser.getBody(),
              parser.getCc());
          ctx.startActivity(mailIntent);
          v.reload();
        }
        else
        {
          Intent intent = new Intent(Intent.ACTION_VIEW);
          intent.setData(Uri.parse(url));
          SettingsActivity.this.startActivity(intent);
        }
        return true;
      }

      private Intent CreateEmailIntent(String address,
                                       String subject,
                                       String body,
                                       String cc)
      {
        Intent intent = new Intent(Intent.ACTION_SEND);
        intent.putExtra(Intent.EXTRA_EMAIL, new String[]{address});
        intent.putExtra(Intent.EXTRA_TEXT, body);
        intent.putExtra(Intent.EXTRA_SUBJECT, subject);
        intent.putExtra(Intent.EXTRA_CC, cc);
        intent.setType("message/rfc822");
        return intent;
      }
    });

    String versionStr = "";
    try
    {
      versionStr = parent.getPackageManager().getPackageInfo(parent.getPackageName(), 0).versionName;
    } catch (final NameNotFoundException e)
    {
      e.printStackTrace();
    }

    new AlertDialog.Builder(parent)
        .setView(alertDialogView)
        .setTitle(String.format(parent.getString(R.string.version), versionStr))
        .setPositiveButton(R.string.close, new DialogInterface.OnClickListener()
        {
          @Override
          public void onClick(DialogInterface dialog, int which)
          {
            dialog.dismiss();
          }
        })
        .create()
        .show();

    myWebView.loadUrl(ABOUT_ASSET_URL);
  }

  private native boolean isDownloadingActive();


  // needed for soft keyboard to appear in alertdialog.
  // check https://code.google.com/p/android/issues/detail?id=7189 for details
  public static class MyWebView extends WebView
  {

    public MyWebView(Context context)
    {
      super(context);
    }

    public MyWebView(Context context, AttributeSet attrs)
    {
      super(context, attrs);
    }

    public MyWebView(Context context, AttributeSet attrs, int defStyle)
    {
      super(context, attrs, defStyle);
    }

    @Override
    public boolean onCheckIsTextEditor()
    {
      return true;
    }
  }
}

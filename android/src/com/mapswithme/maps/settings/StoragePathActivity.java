package com.mapswithme.maps.settings;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.view.View;
import android.widget.ListView;

import com.mapswithme.maps.base.MapsWithMeBaseListActivity;

public class StoragePathActivity extends MapsWithMeBaseListActivity implements StoragePathManager.SetStoragePathListener
{
  private StoragePathManager mPathManager = new StoragePathManager();
  private StoragePathAdapter mAdapter;

  private StoragePathAdapter getAdapter()
  {
    return (StoragePathAdapter) getListView().getAdapter();
  }

  @Override
  protected void onListItemClick(final ListView l, View v, final int position, long id)
  {
    // Do not process clicks on header items.
    if (position != 0)
      getAdapter().onItemClick(position);
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
        if (mAdapter != null)
          mAdapter.updateList(mPathManager.getStorageItems(), mPathManager.getCurrentStorageIndex(), StoragePathManager.getMwmDirSize());
      }
    };
    mPathManager.startExternalStorageWatching(this, receiver, this);
    initAdapter();
    mAdapter.updateList(mPathManager.getStorageItems(), mPathManager.getCurrentStorageIndex(), StoragePathManager.getMwmDirSize());
    setListAdapter(mAdapter);
  }

  @Override
  protected void onPause()
  {
    super.onPause();
    mPathManager.stopExternalStorageWatching();
  }

  private void initAdapter()
  {
    if (mAdapter == null)
      mAdapter = new StoragePathAdapter(mPathManager, this);
  }

  @Override
  public void moveFilesFinished(String newPath)
  {
    mAdapter.updateList(mPathManager.getStorageItems(), mPathManager.getCurrentStorageIndex(), StoragePathManager.getMwmDirSize());
  }

  @Override
  public void moveFilesFailed()
  {
    //
  }
}

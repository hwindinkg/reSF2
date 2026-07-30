package com.fyber.mediation;

import android.app.Activity;
import com.fyber.mediation.facebook.FacebookMediationAdapter;
import com.fyber.utils.FyberLogger;
import java.lang.InterruptedException;
import java.lang.Object;
import java.lang.String;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;

public final class MediationAdapterStarter {
  private static final String TAG = "MediationAdapterStarter";

  public static AdaptersListener adaptersListener;

  private static void startVungle(final Activity activity, final Map<String, Object> configs, final Map<String, MediationAdapter> map) {
    try {
      MediationAdapter adapter = new VungleCompatibilityAdapter();
      FyberLogger.d(TAG, "Starting adapter Vungle with version 3.3.4-r1");
      if (adapter.startAdapter(activity, configs)) {
        FyberLogger.d(TAG, "Adapter Vungle with version 3.3.4-r1 was started successfully");
        map.put("vungle", adapter);
      } else {
        FyberLogger.d(TAG, "Adapter Vungle with version 3.3.4-r1 was not started successfully");
      }
    } catch (Throwable throwable) {
      FyberLogger.e(TAG, "Exception occurred while loading adapter Vungle with version 3.3.4-r1 - " + throwable.getCause());
    }
  }

  private static void startTapjoy(final Activity activity, final Map<String, Object> configs, final Map<String, MediationAdapter> map) {
    try {
      MediationAdapter adapter = new TapjoyCompatibilityAdapter();
      FyberLogger.d(TAG, "Starting adapter Tapjoy with version 11.3.0-r1");
      if (adapter.startAdapter(activity, configs)) {
        FyberLogger.d(TAG, "Adapter Tapjoy with version 11.3.0-r1 was started successfully");
        map.put("tapjoy", adapter);
      } else {
        FyberLogger.d(TAG, "Adapter Tapjoy with version 11.3.0-r1 was not started successfully");
      }
    } catch (Throwable throwable) {
      FyberLogger.e(TAG, "Exception occurred while loading adapter Tapjoy with version 11.3.0-r1 - " + throwable.getCause());
    }
  }

  private static void startAdColony(final Activity activity, final Map<String, Object> configs, final Map<String, MediationAdapter> map) {
    try {
      MediationAdapter adapter = new AdColonyCompatibilityAdapter();
      FyberLogger.d(TAG, "Starting adapter AdColony with version 2.3.3-r1");
      if (adapter.startAdapter(activity, configs)) {
        FyberLogger.d(TAG, "Adapter AdColony with version 2.3.3-r1 was started successfully");
        map.put("adcolony", adapter);
      } else {
        FyberLogger.d(TAG, "Adapter AdColony with version 2.3.3-r1 was not started successfully");
      }
    } catch (Throwable throwable) {
      FyberLogger.e(TAG, "Exception occurred while loading adapter AdColony with version 2.3.3-r1 - " + throwable.getCause());
    }
  }

  private static void startFlurryAppCircleClips(final Activity activity, final Map<String, Object> configs, final Map<String, MediationAdapter> map) {
    try {
      MediationAdapter adapter = new FlurryAppCircleClipsCompatibilityAdapter();
      FyberLogger.d(TAG, "Starting adapter FlurryAppCircleClips with version 6.2.0-r1");
      if (adapter.startAdapter(activity, configs)) {
        FyberLogger.d(TAG, "Adapter FlurryAppCircleClips with version 6.2.0-r1 was started successfully");
        map.put("flurryappcircleclips", adapter);
      } else {
        FyberLogger.d(TAG, "Adapter FlurryAppCircleClips with version 6.2.0-r1 was not started successfully");
      }
    } catch (Throwable throwable) {
      FyberLogger.e(TAG, "Exception occurred while loading adapter FlurryAppCircleClips with version 6.2.0-r1 - " + throwable.getCause());
    }
  }

  private static void startAppLovin(final Activity activity, final Map<String, Object> configs, final Map<String, MediationAdapter> map) {
    try {
      MediationAdapter adapter = new AppLovinCompatibilityAdapter();
      FyberLogger.d(TAG, "Starting adapter AppLovin with version 6.1.5-r1");
      if (adapter.startAdapter(activity, configs)) {
        FyberLogger.d(TAG, "Adapter AppLovin with version 6.1.5-r1 was started successfully");
        map.put("applovin", adapter);
      } else {
        FyberLogger.d(TAG, "Adapter AppLovin with version 6.1.5-r1 was not started successfully");
      }
    } catch (Throwable throwable) {
      FyberLogger.e(TAG, "Exception occurred while loading adapter AppLovin with version 6.1.5-r1 - " + throwable.getCause());
    }
  }

  private static void startApplifier(final Activity activity, final Map<String, Object> configs, final Map<String, MediationAdapter> map) {
    try {
      MediationAdapter adapter = new ApplifierCompatibilityAdapter();
      FyberLogger.d(TAG, "Starting adapter Applifier with version 1.5.6-r2");
      if (adapter.startAdapter(activity, configs)) {
        FyberLogger.d(TAG, "Adapter Applifier with version 1.5.6-r2 was started successfully");
        map.put("applifier", adapter);
      } else {
        FyberLogger.d(TAG, "Adapter Applifier with version 1.5.6-r2 was not started successfully");
      }
    } catch (Throwable throwable) {
      FyberLogger.e(TAG, "Exception occurred while loading adapter Applifier with version 1.5.6-r2 - " + throwable.getCause());
    }
  }

  private static void startInmobi(final Activity activity, final Map<String, Object> configs, final Map<String, MediationAdapter> map) {
    try {
      MediationAdapter adapter = new InmobiCompatibilityAdapter();
      FyberLogger.d(TAG, "Starting adapter Inmobi with version 5.2.3-r1");
      if (adapter.startAdapter(activity, configs)) {
        FyberLogger.d(TAG, "Adapter Inmobi with version 5.2.3-r1 was started successfully");
        map.put("inmobi", adapter);
      } else {
        FyberLogger.d(TAG, "Adapter Inmobi with version 5.2.3-r1 was not started successfully");
      }
    } catch (Throwable throwable) {
      FyberLogger.e(TAG, "Exception occurred while loading adapter Inmobi with version 5.2.3-r1 - " + throwable.getCause());
    }
  }

  private static void startChartboost(final Activity activity, final Map<String, Object> configs, final Map<String, MediationAdapter> map) {
    try {
      MediationAdapter adapter = new ChartboostCompatibilityAdapter();
      FyberLogger.d(TAG, "Starting adapter Chartboost with version 6.4.1-r1");
      if (adapter.startAdapter(activity, configs)) {
        FyberLogger.d(TAG, "Adapter Chartboost with version 6.4.1-r1 was started successfully");
        map.put("chartboost", adapter);
      } else {
        FyberLogger.d(TAG, "Adapter Chartboost with version 6.4.1-r1 was not started successfully");
      }
    } catch (Throwable throwable) {
      FyberLogger.e(TAG, "Exception occurred while loading adapter Chartboost with version 6.4.1-r1 - " + throwable.getCause());
    }
  }

  private static void startFacebookAudienceNetwork(final Activity activity, final Map<String, Object> configs, final Map<String, MediationAdapter> map) {
    try {
      MediationAdapter adapter = new FacebookMediationAdapter();
      FyberLogger.d(TAG, "Starting adapter FacebookAudienceNetwork with version 4.10.0-r2");
      if (adapter.startAdapter(activity, configs)) {
        FyberLogger.d(TAG, "Adapter FacebookAudienceNetwork with version 4.10.0-r2 was started successfully");
        map.put("facebookaudiencenetwork", adapter);
      } else {
        FyberLogger.d(TAG, "Adapter FacebookAudienceNetwork with version 4.10.0-r2 was not started successfully");
      }
    } catch (Throwable throwable) {
      FyberLogger.e(TAG, "Exception occurred while loading adapter FacebookAudienceNetwork with version 4.10.0-r2 - " + throwable.getCause());
    }
  }

  public static Map<String, MediationAdapter> startAdapters(final Activity activity, final Map<String, Map<String, Object>> configs) {
    Map<String, MediationAdapter> map = new HashMap<>();
    startVungle(activity, getConfigsForAdapter(configs, "Vungle"), map);
    startTapjoy(activity, getConfigsForAdapter(configs, "Tapjoy"), map);
    startAdColony(activity, getConfigsForAdapter(configs, "AdColony"), map);
    startFlurryAppCircleClips(activity, getConfigsForAdapter(configs, "FlurryAppCircleClips"), map);
    startAppLovin(activity, getConfigsForAdapter(configs, "AppLovin"), map);
    startApplifier(activity, getConfigsForAdapter(configs, "Applifier"), map);
    startInmobi(activity, getConfigsForAdapter(configs, "Inmobi"), map);
    startChartboost(activity, getConfigsForAdapter(configs, "Chartboost"), map);
    startFacebookAudienceNetwork(activity, getConfigsForAdapter(configs, "FacebookAudienceNetwork"), map);
    return map;
  }

  public static int getAdaptersCount() {
    return 9;
  }

  private static Map<String, Object> getConfigsForAdapter(Map<String, Map<String, Object>> configs, String adapter) {
    Map<String, Object> config = configs.get(adapter.toLowerCase());
    if (config == null) {
      config = Collections.emptyMap();
    }
    return config;
  }

  private static Map<String, Map<String, Object>> getConfigs(final Future<Map<String, Map<String, Object>>> futureConfig) {
    Map<String, Map<String, Object>> configs = MediationConfigProvider.getConfigs();
    Map<String, Map<String, Object>> runtimeConfigs = MediationConfigProvider.getRuntimeConfigs();
    configs = mergeConfigs(configs, runtimeConfigs);
    try {
      if (futureConfig != null) {
        Map<String, Map<String, Object>> serverConfigs = futureConfig.get();
        configs = mergeConfigs(configs, serverConfigs);
      }
    } catch (InterruptedException | ExecutionException e) {
      FyberLogger.e(TAG, "Exception occurred", e);
    }
    return configs;
  }

  private static Map<String, Map<String, Object>> mergeConfigs(final Map<String, Map<String, Object>> intoConfigs, final Map<String, Map<String, Object>> fromConfigs) {
    if (fromConfigs != null && !fromConfigs.isEmpty()) {
      for (Map.Entry<String, Map<String, Object>> entry: fromConfigs.entrySet()) {
        String network = entry.getKey();
        Map<String, Object> adapterIntoConfigs = entry.getValue();
        Map<String, Object> adapterFromConfigs = intoConfigs.get(network);
        if (adapterFromConfigs != null) {
          adapterIntoConfigs.putAll(adapterFromConfigs);
        }
        intoConfigs.put(network, adapterIntoConfigs);
      }
    } else {
      FyberLogger.d(TAG, "There were no configurations to override");
    }
    return intoConfigs;
  }

  public static Map<String, MediationAdapter> startAdapters(final Activity activity, final Future<Map<String, Map<String, Object>>> future) {
    Map<String, Map<String, Object>> configs = getConfigs(future);
    Map<String, MediationAdapter> adapters = startAdapters(activity, configs);
    if (adaptersListener != null) {
      adaptersListener.startedAdapters(adapters.keySet(), configs);
    }
    return adapters;
  }
}

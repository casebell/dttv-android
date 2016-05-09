package dttv.app;

import java.io.IOException;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

import javax.microedition.khronos.opengles.GL10;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ConfigurationInfo;
import android.content.res.Configuration;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.MotionEvent;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.View.OnTouchListener;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.LinearLayout.LayoutParams;
import android.widget.ProgressBar;
import android.widget.RelativeLayout;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;
import android.widget.Toast;
import dttv.app.DtPlayer.OnCompletionListener;
import dttv.app.DtPlayer.OnFreshVideo;
import dttv.app.DtPlayer.OnPreparedListener;
import dttv.app.compnent.PopWindowCompnent;
import dttv.app.impl.ICallBack;
import dttv.app.utils.Constant;
import dttv.app.utils.ControlLightness;
import dttv.app.utils.Log;
import dttv.app.utils.TimesUtil;
import dttv.app.utils.VolumeUtil;
import dttv.app.widget.GlVideoView;
import dttv.app.widget.OnTouchMoveListener;
/**
 * VideoPlayer Activity
 * 
 * @author shihx1
 *	
 */
public class VideoPlayerActivity extends Activity implements OnClickListener,OnTouchListener{
	private String TAG = "VideoPlayerActivity";
	private DtPlayer dtPlayer;
	private String mPath;
	
	
	private View mBarView;
	private RelativeLayout playerBarLay,playerRootviewLay;
	private RelativeLayout topBarLay;
	private ImageButton rotateBtn;
	private TextView currentTimeTxt,totalTimeTxt,media_name_txt,dt_decoder_btn;
	private ImageButton preBtn,nextBtn,pauseBtn,ratioBtn;
	private Button effectBtn;
	private SeekBar playerProgressBar;
	private GlVideoView glSurfaceView;
	private int seek_flag = 0;
	private boolean isDisableScale= false;
	
	private static final int PLAYER_IDLE = 0x0;
	private static final int PLAYER_INITING = 0x1;
	private static final int PLAYER_PREPARED = 0x2;
	private static final int PLAYER_RUNNING = 0x3;
	private static final int PLAYER_PAUSED = 0x4;
	private static final int PLAYER_BUFFERING = 0x5;
	private static final int PLAYER_SEEKING = 0x6;
	
	private static final int PLAYER_STOP = 0x100;
	private static final int PLAYER_EXIT = 0x101;
	
	
	
	private int mState = PLAYER_IDLE;
    
    private final int SCREEN_ORIGINAL = 0;
	private final int SCREEN_169value = 1;
    private final int SCREEN_FULLSCREEN = 2;
    private final int SCREEN_43value = 3;
    private final int SCREEN_NORMALSCALE = 4;

    
    private int screenHeight,screenWidth;
    private int surface_width = 320;
    private int surface_height = 240;
    private int currentPosition = -1;
    
    private final int HANDLE_UP = 0x0110;
	private final int HANDLE_DOWN = HANDLE_UP+1;
	private ProgressBar brightProgressBar,volumeProgressBar;
	private VolumeUtil volumeUtil;
	private int mScreenWidth;
	private int mScreenHeight;
	private int currentLightness;
	
	@SuppressLint("ShowToast")
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		// TODO Auto-generated method stub
		super.onCreate(savedInstanceState);
		requestWindowFeature(Window.FEATURE_NO_TITLE);
		setContentView(R.layout.video_play);
		Log.i(TAG, "enter onCreate");
		mState = PLAYER_IDLE;		
		dtPlayer = new DtPlayer(this);
		if(OpenglES2Support() == 0)
		{
			Toast.makeText(this, "opengl es2.0 not supported， Exit!", 1).show();
			return;
		}
		else
		{
			//opengl
			glSurfaceView = (GlVideoView)findViewById(R.id.glvideo_view);
			glSurfaceView.setRenderer(new GLSurfaceViewRender());
			glSurfaceView.setTouchMoveListener(new GLMoveTouchListener());
	        //glSurfaceView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
	        glSurfaceView.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
	        //glSurfaceView.setOnTouchListener((OnTouchListener) this);
		}

		getWindow().setBackgroundDrawableResource(R.color.bg_black);
		
		initView();
		initDisplay();
		initExtraData();
		initListener();
	}
	
	private void initDisplay(){
		DisplayMetrics displayMetrics =  new DisplayMetrics();
		getWindowManager().getDefaultDisplay().getMetrics(displayMetrics);
		mScreenWidth = displayMetrics.widthPixels;
		mScreenHeight = displayMetrics.heightPixels;
		if(mScreenWidth==0){
			Display display = getWindowManager().getDefaultDisplay();
			mScreenWidth = display.getWidth();
			mScreenHeight = display.getHeight();
		}
	}
	
	private int OpenglES2Support()
	{
		ActivityManager activityManager = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ConfigurationInfo configurationInfo = activityManager.getDeviceConfigurationInfo();
 
	    boolean supportsEs2 = configurationInfo.reqGlEsVersion >= 0x20000 || isProbablyEmulator();	 
	    if(supportsEs2)
	    	return 1;
	    else
	    	return 0;
	}
	
	private boolean isProbablyEmulator() {
	    return Build.VERSION.SDK_INT >= Build.VERSION_CODES.ICE_CREAM_SANDWICH_MR1
	            && (Build.FINGERPRINT.startsWith("generic")
	                    || Build.FINGERPRINT.startsWith("unknown")
	                    || Build.MODEL.contains("google_sdk")
	                    || Build.MODEL.contains("Emulator")
	                    || Build.MODEL.contains("Android SDK built for x86"));
	}
	
	@SuppressLint("ShowToast")
	private void initExtraData(){
		Intent intent = getIntent();
		mPath = intent.getStringExtra(Constant.FILE_MSG);
		String mediaName = intent.getStringExtra(Constant.MEIDA_NAME_STR);
		media_name_txt.setText(mediaName);
		
		//----------------------add type--------------------------------------
		int mType = intent.getIntExtra(Constant.FILE_TYPE,-1);
		resolveTypeView(mType);
		Toast.makeText(this, "mPath is:"+mPath, 1).show();
		try {
			mState = PLAYER_INITING;
			if(dtPlayer.setDataSource(mPath) == -1)
			{
				mState = PLAYER_IDLE;
				return;
			}
			//here to set video size
			
			int width = dtPlayer.getVideoWidth();
			int height = dtPlayer.getVideoHeight();
			Log.d(TAG,"--width:"+width+"  height:"+height);
			if(width > 0 && height > 0 && width <= 1920 && height <= 1088)
			{
				ViewGroup.LayoutParams layoutParams=glSurfaceView.getLayoutParams();
				layoutParams.width=width;
				layoutParams.height=height;
				glSurfaceView.setLayoutParams(layoutParams);				
				//dtPlayer.setVideoSize(width,height);
			}
			dtPlayer.prepare();
		} catch (IllegalArgumentException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} catch (SecurityException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} catch (IllegalStateException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		currentLightness = ControlLightness.getInstance().getLightness(this);
		if(currentLightness>=255){
			currentLightness = 255;
		}else if(currentLightness<=0){
			currentLightness = 0;
		}
	}
	
	private void initView(){
		mBarView = (View)findViewById(R.id.audio_player_bar_lay);
		playerBarLay = (RelativeLayout)mBarView.findViewById(R.id.audio_player_bar_lay);
		playerRootviewLay = (RelativeLayout)findViewById(R.id.dt_player_rootview);
		topBarLay = (RelativeLayout)findViewById(R.id.dt_top_play_bar_lay);
		media_name_txt = (TextView)findViewById(R.id.dt_media_name_txt);
		dt_decoder_btn = (TextView)findViewById(R.id.dt_decoder_btn);
		rotateBtn = (ImageButton)findViewById(R.id.dt_player_rotate_btn);
		currentTimeTxt = (TextView)mBarView.findViewById(R.id.dt_play_current_time);
		totalTimeTxt = (TextView)mBarView.findViewById(R.id.dt_play_total_time);
		preBtn = (ImageButton)mBarView.findViewById(R.id.dt_play_prev_btn);
		pauseBtn = (ImageButton)mBarView.findViewById(R.id.dt_play_pause_btn);
		nextBtn = (ImageButton)mBarView.findViewById(R.id.dt_play_next_btn);
		effectBtn = (Button)mBarView.findViewById(R.id.dt_play_effect_btn);
		ratioBtn = (ImageButton)mBarView.findViewById(R.id.dt_play_ratio_btn);
		playerProgressBar = (SeekBar)mBarView.findViewById(R.id.dt_play_progress_seekbar);
		DisplayMetrics dm = new DisplayMetrics();
		getWindowManager().getDefaultDisplay().getMetrics(dm);
		screenHeight = dm.heightPixels;
		screenWidth = dm.widthPixels;
		
		ratioBtn.setBackgroundResource(R.drawable.dt_player_control_ratio_16_9);
		
		brightProgressBar = (ProgressBar)findViewById(R.id.bright_progressbar);
		volumeProgressBar = (ProgressBar)findViewById(R.id.volume_progress);
		
		volumeUtil = new VolumeUtil(this);
		volumeProgressBar.setMax(volumeUtil.getMaxVolume());
		volumeProgressBar.setProgress(volumeUtil.getCurrentVolume());
	}
	
	private void resolveTypeView(int type){
		switch(type){
		case Constant.LOCAL_VIDEO:
			effectBtn.setVisibility(View.GONE);
			ratioBtn.setVisibility(View.VISIBLE);
			break;
		case Constant.LOCAL_AUDIO:
			effectBtn.setVisibility(View.VISIBLE);
			ratioBtn.setVisibility(View.GONE);
			break;
		}
	}
	
	private void initListener(){
		dtPlayer.setOnPreparedListener(new PrePareListener());
		dtPlayer.setOnFreshVideo(new FreshVideo());
		dtPlayer.setOnCompletionListener(new OnCompleteListener());
		playerProgressBar.setOnSeekBarChangeListener(new OnSeekChangeListener());
		preBtn.setOnClickListener(this);
		nextBtn.setOnClickListener(this);
		pauseBtn.setOnClickListener(this);
		ratioBtn.setOnClickListener(this);
		effectBtn.setOnClickListener(this);
		ratioBtn.setOnClickListener(this);
		rotateBtn.setOnClickListener(this);
		playerBarLay.setOnTouchListener(this);
		topBarLay.setOnTouchListener(this);
		rotateBtn.setOnTouchListener(this);
		dt_decoder_btn.setOnClickListener(this);
		playerRootviewLay.setOnTouchListener(this);
	}
	
	class PrePareListener implements OnPreparedListener{
		@Override
		public void onPrepared(DtPlayer mp) {
			// TODO Auto-generated method stub
			Log.i(Constant.LOGTAG, "enter onPrepared");
			mState = PLAYER_PREPARED;
			dtPlayer.start();
			mState = PLAYER_RUNNING;
			int duration = mp.getDuration();
            if(duration>0){
            	totalTimeTxt.setText(TimesUtil.getTime(duration));
            	playerProgressBar.setMax(duration);
            }
            startTimerTask();
            //setVideoScale(1);
		}
	}
	
	class FreshVideo implements OnFreshVideo{
		@Override
		public void onFresh(DtPlayer mp) {
			// TODO Auto-generated method stub
			//Log.i(Constant.LOGTAG, "fresh video");
			glSurfaceView.requestRender();
			
		}
	}
	
	class OnCompleteListener implements OnCompletionListener{
		@Override
		public void onCompletion(DtPlayer mp) {
			// TODO Auto-generated method stub
			mState = PLAYER_EXIT;
			finish();
		}
	}
	
	class OnSeekChangeListener implements OnSeekBarChangeListener{

		@Override
		public void onProgressChanged(SeekBar seekBar, int progress,
				boolean fromUser) {
			// TODO Auto-generated method stub
			if(seek_flag == 1)
			{
				//int currentTime = seekBar.getProgress();
				//dtPlayer.seekTo(currentTime);
			}
		}

		@Override
		public void onStartTrackingTouch(SeekBar seekBar) {
			// TODO Auto-generated method stub
			seek_flag = 1;
		}

		@Override
		public void onStopTrackingTouch(SeekBar seekBar) {
			// TODO Auto-generated method stub
			mState = PLAYER_SEEKING;
			int currentTime = seekBar.getProgress();
			dtPlayer.seekTo(currentTime);
			dtPlayer.start();
			seek_flag = 0;
		}
		
	}
	@Override
	public void onConfigurationChanged(Configuration newConfig) {
		super.onConfigurationChanged(newConfig);
		if(dtPlayer!=null){
			dtPlayer.seekTo(currentPosition);
			dtPlayer.start();
		}
	};
	
	Handler doActionHandler = new Handler(Looper.getMainLooper()){
		public void handleMessage(android.os.Message msg) {
			int msgId = msg.what;
			switch(msgId){
			case Constant.REFRESH_TIME_MSG:
				int currentTime = dtPlayer.getCurrentPosition();
				currentPosition = currentTime;
				int duration = dtPlayer.getDuration();
				if(currentTime < 0)
					currentTime = 0;
				if(currentTime > duration)
					currentTime = duration;
				currentTimeTxt.setText(TimesUtil.getTime(currentTime));
				playerProgressBar.setProgress(currentTime);
				break;
			case Constant.BEGIN_MEDIA_MSG:
	            //startTimerTask();
				break;
			case Constant.HIDE_OPREATE_BAR_MSG:
				//playerBarLay.setVisibility(View.GONE);
				Log.i(TAG, "enter HIDE_OPREATE_BAR_MSG");
				showToolsBar(false);
				break;
			case Constant.HIDE_PROGRESS_BAR_MSG:
				showProgressBar(false);
				break;
			}
		};
	};
	
	private Timer mTimer;
	private void startTimerTask(){
		mTimer = new Timer();
		mTimer.schedule(new TimerTask() {
			
			@Override
			public void run() {
				// TODO Auto-generated method stub
				doActionHandler.sendEmptyMessage(Constant.REFRESH_TIME_MSG);
			}
		}, Constant.REFRESH_TIME, Constant.REFRESH_TIME);
	}
	
	private void releaseTimerAndHandler(){
		//isEnableTime = false;
		if(mTimer!=null)
			mTimer.cancel();
		doActionHandler.removeCallbacksAndMessages(null);
	}

    @Override
    public void onPause()
    {
    	releaseTimerAndHandler();
    	glSurfaceView.onPause();
    	dtPlayer.pause();
        super.onPause();
        if(mState == PLAYER_PAUSED)
        	mState = PLAYER_RUNNING;
        if(mState == PLAYER_RUNNING)
        	mState = PLAYER_PAUSED;
        Log.d(TAG,"--PAUSE--");
    }
	
    @Override
    protected void onResume() {
        super.onResume();
        //glSurfaceView.onResume();
        Log.i(TAG, "enter onResume dtPlayer is:"+dtPlayer);
        if(dtPlayer != null){
        	dtPlayer.seekTo(currentPosition);
        	dtPlayer.start();
        }
    }
	
	@Override
	protected void onStop() {
		// TODO Auto-generated method stub
		Log.i(TAG, "enterStop");
		mState = PLAYER_STOP;
		//dtPlayer.release();
		dtPlayer.stop();
		super.onStop();
	}
	
	

	@Override
	public void onClick(View v) {
		// TODO Auto-generated method stub
		//showAudioEffect(false);
		switch(v.getId()){
		case R.id.dt_play_next_btn:
			
			break;
		case R.id.dt_play_prev_btn:
			break;
		case R.id.dt_play_pause_btn:
			handlePausePlay();
			break;
		case R.id.dt_play_ratio_btn:
			//Fix
			setVideoScale(temp_flag);
			break;
		case R.id.dt_player_rotate_btn:
			changeConfigration();
			break;
		case R.id.dt_play_effect_btn://for audio effect
			showAudioEffect(true);
			break;
		case R.id.dt_decoder_btn:
			
			break;
		}
	}
	
	private PopWindowCompnent audioCompnent;
	private void showAudioEffect(boolean isShowing){
		
		if(audioCompnent==null)//
			audioCompnent = new PopWindowCompnent(this,this);
		audioCompnent.show(effectBtn, isShowing);
		audioCompnent.setCallback(new ICallBack() {
			@Override
			public void doItemClickListener(AdapterView<?> parent, View v,
					int position, long id) {
				// TODO Auto-generated method stub
				super.doItemClickListener(parent, v, position, id);
				TextView effectTxt = (TextView)v.findViewById(android.R.id.text1);
				String effectStr = effectTxt.getText().toString();
				String effectStr2 = Constant.gEqulizerPresets[position];
				//Toast.makeText(mActivity, "effectStr is:"+effectStr+"-0-0 effectStr2 is:"+effectStr2, Toast.LENGTH_LONG).show();
				if(dtPlayer!=null){
					dtPlayer.setAuxEffectSendLevel(position);
					effectBtn.setText(effectStr);
				}
			}
		});
	}
	
	/**
	 * 横竖屏幕切换
	 */
	private void changeConfigration(){
		if(getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE){
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
		}else{
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
		}
	}
	
	private void showToolsBar(boolean isNeed){
		playerBarLay.setVisibility(isNeed == true ? View.VISIBLE : View.GONE);
		topBarLay.setVisibility(isNeed == true ? View.VISIBLE : View.GONE);
		rotateBtn.setVisibility(isNeed == true ? View.VISIBLE : View.GONE);
	}
	
	private void showProgressBar(boolean isShow){
		brightProgressBar.setVisibility(isShow == true ? View.VISIBLE : View.GONE);
		volumeProgressBar.setVisibility(isShow == true ? View.VISIBLE : View.GONE);
	}
	
	private void handlePausePlay(){
		try {
			if(dtPlayer.isPlaying()){
				dtPlayer.pause();
				pauseBtn.setBackgroundResource(R.drawable.btn_mu_pause);
			}else{
				dtPlayer.start();
				pauseBtn.setBackgroundResource(R.drawable.btn_mu_play);
			}
		} catch (IllegalStateException e) {
			// TODO: handle exception
		} catch (Exception e) {
			// TODO: handle exception
		}
	}
	
	
	@Override
	protected void onDestroy() {
		// TODO Auto-generated method stub
		Log.i(TAG, "enter onDestroy");
		dtPlayer.stop();
		dtPlayer.release();
		dtPlayer = null;
		super.onDestroy();
	}
	
	//--------------------------onMoveTouch-------------------------//
	
	class GLMoveTouchListener implements OnTouchMoveListener{

		@Override
		public void onTouchMoveUp(float posX) {
			// TODO Auto-generated method stub
			if(posX<(mScreenWidth/2-10)){//left up handle audiovolume
				//Log.i(TAG, "left up handle audiovolume");
				handleAudioVolume(HANDLE_UP);
			}else if(posX>(mScreenWidth/2+10)){//right up handle lightless
				//Log.i(TAG, "right up handle lightless");
				handleLightless(HANDLE_UP);
			}
		}

		@Override
		public void onTouchMoveDown(float posX) {
			// TODO Auto-generated method stub
			if(posX<(mScreenWidth/2-10)){//left down handle audiovolume
				Log.i(TAG, "left down handle audiovolume");
				handleAudioVolume(HANDLE_DOWN);
			}else if(posX>(mScreenWidth/2+10)){//right down handle lightless
				//Log.i(TAG, "right down handle lightless");
				handleLightless(HANDLE_DOWN);
			}
		}

		@Override
		public void onTouch(MotionEvent event) {
			// TODO Auto-generated method stub
			showToolsBar(true);
			doActionHandler.removeMessages(Constant.HIDE_OPREATE_BAR_MSG);
			doActionHandler.sendEmptyMessageDelayed(Constant.HIDE_OPREATE_BAR_MSG, 5*Constant.REFRESH_TIME);
		}
	}
		
	//---------------------------OPENGL------------------------------//
	
	class GLSurfaceViewRender implements GLSurfaceView.Renderer {  
  
		private Lock lock = new ReentrantLock();

		@Override
		public void onSurfaceCreated(GL10 gl,
				javax.microedition.khronos.egl.EGLConfig config) {
			// TODO Auto-generated method stub
			Log.i(TAG, "gl create enter");			
			//gl.glClearColor(0.0f, 0f, 1f, 0.5f); // display blue at first
			//gl.glClear(GL10.GL_COLOR_BUFFER_BIT);
			dtPlayer.onSurfaceCreated();
			
		}
		
        @Override  
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            //other case
        	lock.lock();
        	Log.i(TAG, "gl surface change enter, width:"+width+" height:"+height);
        	dtPlayer.onSurfaceChanged(width,height);
        	surface_width = width;
        	surface_height = height;
        	
        	lock.unlock();
        }
  
        @Override  
        public void onDrawFrame(GL10 gl) {
            //Log.i(TAG, "onDrawFrame");  
            // 清除屏幕和深度缓存(如果不调用该代码, 将不显示glClearColor设置的颜色)  
            // 同样如果将该代码放到 onSurfaceCreated 中屏幕会一直闪动  
            //gl.glClear(GL10.GL_COLOR_BUFFER_BIT);
        	//Log.i(TAG, "draw enter");
        	lock.lock();
        	dtPlayer.onDrawFrame();
        	lock.unlock();
        }	
    }  
	
	private int temp_flag = 0;
	private void setVideoScale(int flag){
		temp_flag ++;
		flag = temp_flag % 3;
		Log.i(TAG, "setVideoScale flag is:"+flag);
		//LayoutParams lp = (LayoutParams) glSurfaceView.getLayoutParams();
		LayoutParams lp = new LayoutParams(surface_width, surface_height);
		Log.i(TAG, "begin");
		switch(flag){
		case SCREEN_169value:
			if(screenWidth * 9 > screenHeight * 16){
				lp.height = screenHeight;
				lp.width = screenHeight*16/9;
			}else{
				lp.height = screenWidth *9/16;
				lp.width = screenWidth;
			}
			ratioBtn.setBackgroundResource(R.drawable.dt_player_control_ratio_fullscreen);
			break;
		case SCREEN_43value:
			if(screenWidth*3 > screenHeight*4){
				lp.height = screenHeight;
				lp.width = screenHeight * 4 /3;
			}else{
				lp.height = screenWidth * 3/4;
				lp.width = screenWidth;
			}
			ratioBtn.setBackgroundResource(R.drawable.dt_player_control_ratio_1_1);
			break;
		case SCREEN_ORIGINAL:
			lp.width = dtPlayer.getVideoWidth();
			lp.height = dtPlayer.getVideoHeight();
			ratioBtn.setBackgroundResource(R.drawable.dt_player_control_ratio_16_9);
			break;
		case SCREEN_FULLSCREEN:
			lp.width = screenWidth;
			lp.height = screenHeight;
			Log.i(TAG, "SCREEN_FULLSCREEN lp.width is:"+lp.width+"----lp.height is:"+lp.height);
			ratioBtn.setBackgroundResource(R.drawable.dt_player_control_ratio_normal);
			break;
		case SCREEN_NORMALSCALE:
			lp.width = screenWidth;
			lp.height = screenHeight;
			int temp_width = 0;
			int temp_height = 0;
			if(surface_width > 0){
				if(lp.width/surface_width > lp.height/surface_height){
					temp_width = (int)surface_width *lp.height/surface_height;
					temp_height = lp.height;
				}else{
					temp_width = lp.width;
					temp_height = surface_height * lp.width/surface_width;
				}
			}else{
				
			}
			lp.width = temp_width;
			lp.height = temp_height;
			ratioBtn.setBackgroundResource(R.drawable.dt_player_control_ratio_16_9);
			break;
		}
		Log.i(TAG, "lp.width is:"+lp.width+"----lp.height is:"+lp.height);
		//glSurfaceView.setLayoutParams(lp);
		Log.i(TAG, "before setVideoSize");
		
		ViewGroup.LayoutParams layoutParams=glSurfaceView.getLayoutParams();
		layoutParams.width=lp.width;
		layoutParams.height=lp.height;
		glSurfaceView.setLayoutParams(layoutParams);
	
//		dtPlayer.setVideoSize(lp.width, lp.height);
//		Log.i(TAG, "after setVideoSize");
		//dtPlayer.onSurfaceChanged(lp.width, lp.height);
	}

	@Override
	public boolean onTouch(View v, MotionEvent event) {
		// TODO Auto-generated method stub
		Log.i(TAG, "enter onTouch");
		if(v!= effectBtn){
			showAudioEffect(false);
		}
		showToolsBar(true);
		doActionHandler.removeMessages(Constant.HIDE_OPREATE_BAR_MSG);
		doActionHandler.sendEmptyMessageDelayed(Constant.HIDE_OPREATE_BAR_MSG, 5*Constant.REFRESH_TIME);
		return false;
	}
	
	private void handleAudioVolume(int type){
		switch(type){
		case HANDLE_UP:
			volumeUtil.upVolume(0);
			break;
		case HANDLE_DOWN:
			volumeUtil.downVolume(0);
			break;
		}
		volumeProgressBar.setVisibility(View.VISIBLE);
		volumeProgressBar.setProgress(volumeUtil.getCurrentVolume());
		doActionHandler.removeMessages(Constant.HIDE_PROGRESS_BAR_MSG);
		doActionHandler.sendEmptyMessageDelayed(Constant.HIDE_PROGRESS_BAR_MSG, 5*Constant.REFRESH_TIME);
	}
	
	private void handleLightless(int type){
		currentLightness = ControlLightness.getInstance().getLightness(this);
		Log.i(TAG, "currentLightness is:"+currentLightness);
		switch(type){
		case HANDLE_UP:
			currentLightness+=5;
			currentLightness = currentLightness >= 255 ? 255:currentLightness;
			break;
		case HANDLE_DOWN:
			currentLightness-=5;
			currentLightness = currentLightness <= 0 ? 0 : currentLightness;
			break;
		}
		ControlLightness.getInstance().setBrightness(this, currentLightness);
		brightProgressBar.setVisibility(View.VISIBLE);
		brightProgressBar.setProgress(currentLightness);
		doActionHandler.removeMessages(Constant.HIDE_PROGRESS_BAR_MSG);
		doActionHandler.sendEmptyMessageDelayed(Constant.HIDE_PROGRESS_BAR_MSG, 5*Constant.REFRESH_TIME);
	}
	
}

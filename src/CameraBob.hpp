#ifndef CAMERABOB_H
#define CAMERABOB_H

class CameraBob {

  private:
	float x = 0;
	float y = 0;
	float s = 0;
	float a = 0;
	float f = 0;

  public:
	CameraBob() {
	}

	void SinWave(float speed, float amplitude, float frequency);
	void update(float delta);
	float getX();

	float getY();
	float getSpeed();
	float getAmplitude();
	float getFrequency();
	void setX(float x2);
	void setY(float y1);
	void setSpeed(float speed);
	void setAmplitude(float amplitude);
	void setFrequency(float frequency);
};

#endif // CAMERABOB_H
